/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/bus.h>
#include <modvm/bug.h>
#include <modvm/log.h>

#undef pr_fmt
#define pr_fmt(fmt) "bus: " fmt

/*
 * Global lists maintaining the system's address maps.
 * Currently, we assume all devices are registered during the machine
 * initialization phase before vCPUs are energized. Thus, lockless
 * iteration during VM-exits is safe. If dynamic device hot-plugging is
 * introduced later, these lists must be protected by an RCU mechanism
 * or a read-write spinlock.
 */
static LIST_HEAD(pio_regions);
static LIST_HEAD(mmio_regions);

/**
 * bus_register_region - map a device to a specific address range on the system bus
 * @space: the address space to map into (PIO or MMIO)
 * @base: the starting address on the bus
 * @length: the size of the mapping in bytes
 * @dev: pointer to the device instance claiming the region
 *
 * Return: 0 on success, negative error code on failure.
 */
int bus_register_region(enum vm_bus_space space, uint64_t base, uint64_t length,
			struct vm_device *dev)
{
	struct vm_bus_region *region;
	struct list_head *target_list;
	struct vm_bus_region *pos;

	if (WARN_ON(!dev || !dev->ops || length == 0))
		return -EINVAL;

	target_list = (space == VM_BUS_SPACE_PIO) ? &pio_regions :
						    &mmio_regions;

	/*
	 * Scan the existing topology for overlapping regions to prevent
	 * hardware resource collisions.
	 */
	list_for_each_entry(pos, target_list, node)
	{
		if (base < pos->base_addr + pos->length &&
		    base + length > pos->base_addr) {
			pr_err("Bus conflict detected at base 0x%lx (space %d)\n",
			       base, space);
			return -EBUSY;
		}
	}

	region = calloc(1, sizeof(*region));
	if (!region)
		return -ENOMEM;

	region->dev = dev;
	region->base_addr = base;
	region->length = length;
	region->space_type = space;

	list_add_tail(&region->node, target_list);

	pr_debug("Registered '%s' to bus space %d at 0x%lx (size %lu)\n",
		 dev->name ? dev->name : "unknown_device", space, base, length);

	return 0;
}

/**
 * bus_dispatch_read - route an inbound read request from a vCPU
 * @space: the targeted address space (PIO or MMIO)
 * @addr: the absolute address on the bus
 * @size: access width in bytes
 *
 * Iterates through the registered regions to find the device that owns
 * the target address. The address is translated to a device-relative
 * offset before invoking the device's read callback.
 *
 * Return: the value supplied by the device, or ~0ULL if unmapped.
 */
uint64_t bus_dispatch_read(enum vm_bus_space space, uint64_t addr, uint8_t size)
{
	struct list_head *target_list;
	struct vm_bus_region *pos;
	uint64_t offset;

	target_list = (space == VM_BUS_SPACE_PIO) ? &pio_regions :
						    &mmio_regions;

	list_for_each_entry(pos, target_list, node)
	{
		if (addr >= pos->base_addr &&
		    addr < pos->base_addr + pos->length) {
			if (pos->dev->ops->read) {
				offset = addr - pos->base_addr;
				return pos->dev->ops->read(pos->dev, offset,
							   size);
			}
			/* Device exists but does not support read operations */
			return 0;
		}
	}

	/*
	 * Hardware floating bus behavior: unmapped regions typically
	 * return all 1s due to pull-up resistors on the data lines.
	 */
	return ~0ULL;
}

/**
 * bus_dispatch_write - route an outbound write request from a vCPU
 * @space: the targeted address space (PIO or MMIO)
 * @addr: the absolute address on the bus
 * @value: the data payload being written
 * @size: access width in bytes
 */
void bus_dispatch_write(enum vm_bus_space space, uint64_t addr, uint64_t value,
			uint8_t size)
{
	struct list_head *target_list;
	struct vm_bus_region *pos;
	uint64_t offset;

	target_list = (space == VM_BUS_SPACE_PIO) ? &pio_regions :
						    &mmio_regions;

	list_for_each_entry(pos, target_list, node)
	{
		if (addr >= pos->base_addr &&
		    addr < pos->base_addr + pos->length) {
			if (pos->dev->ops->write) {
				offset = addr - pos->base_addr;
				pos->dev->ops->write(pos->dev, offset, value,
						     size);
			}
			return;
		}
	}
}