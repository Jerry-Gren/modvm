/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/bus.h>
#include <modvm/core/devres.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/log.h>

#undef pr_fmt
#define pr_fmt(fmt) "bus: " fmt

/*
 * Global lists maintaining the system's address maps.
 * Currently, we assume all devices are registered during the machine
 * initialization phase before virtual processors are energized. Thus,
 * lockless iteration during hypervisor VM-exits is safe.
 */
static LIST_HEAD(pio_regions);
static LIST_HEAD(mmio_regions);

/**
 * bus_region_release - automatically unregister a bus region upon device destruction.
 * @dev: the peripheral device owning the region.
 * @res: the resource payload containing the bus region.
 *
 * This callback is invoked by the devres framework. It safely detaches
 * the region from the global bus topology before the memory is freed.
 */
static void bus_region_release(struct vm_device *dev, void *res)
{
	struct vm_bus_region *reg = res;

	(void)dev;
	list_del(&reg->node);
	pr_debug("automatically unregistered region at 0x%lx\n", reg->base);
}

/**
 * vm_bus_register_region - map a device into the system address space.
 * @type: indicates whether this is PIO or MMIO.
 * @base: absolute starting address on the system bus.
 * @size: size of the claimed region in bytes.
 * @dev: the peripheral device owning this region.
 *
 * Scans the existing topology for overlapping regions to prevent
 * hardware resource collisions before registering the new mapping.
 * The mapping is tied to the device's lifecycle via devres.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_bus_register_region(enum vm_bus_type type, uint64_t base, uint64_t size,
			   struct vm_device *dev)
{
	struct vm_bus_region *reg;
	struct vm_bus_region *pos;
	struct list_head *list;

	if (WARN_ON(!dev || !dev->ops || size == 0))
		return -EINVAL;

	list = (type == VM_BUS_PIO) ? &pio_regions : &mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (base < pos->base + pos->size && base + size > pos->base) {
			pr_err("bus conflict detected at base 0x%lx\n", base);
			return -EBUSY;
		}
	}

	reg = vm_devres_alloc(bus_region_release, sizeof(*reg));
	if (!reg)
		return -ENOMEM;

	reg->dev = dev;
	reg->base = base;
	reg->size = size;
	reg->type = type;

	list_add_tail(&reg->node, list);

	vm_devres_add(dev, reg);

	pr_debug("registered '%s' to space %d at 0x%lx\n",
		 dev->name ? dev->name : "unknown", type, base);

	return 0;
}

/**
 * vm_bus_dispatch_read - route a read request to the appropriate peripheral.
 * @type: the target address space (PIO or MMIO).
 * @addr: the absolute physical address requested by the vCPU.
 * @size: the width of the read operation in bytes.
 *
 * Resolves the device mapping and translates the absolute bus address
 * into a relative offset for the device driver.
 *
 * return: the data returned by the device, or ~0ULL for unmapped regions.
 */
uint64_t vm_bus_dispatch_read(enum vm_bus_type type, uint64_t addr,
			      uint8_t size)
{
	struct vm_bus_region *pos;
	struct list_head *list;
	uint64_t offset;

	list = (type == VM_BUS_PIO) ? &pio_regions : &mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (addr >= pos->base && addr < pos->base + pos->size) {
			offset = addr - pos->base;

			/* Strict boundary enforcement to prevent hypervisor out-of-bounds access */
			if (offset + size > pos->size) {
				pr_warn("cross-boundary read intercepted at offset 0x%lx\n",
					offset);
				return ~0ULL;
			}

			if (pos->dev->ops->read)
				return pos->dev->ops->read(pos->dev, offset,
							   size);

			return 0;
		}
	}

	/* Simulate floating bus behavior where unmapped lines return high */
	return ~0ULL;
}

/**
 * vm_bus_dispatch_write - Route a write request to the appropriate peripheral.
 * @type: The target address space (PIO or MMIO).
 * @addr: The absolute physical address targeted by the vCPU.
 * @val: The payload to be written.
 * @size: The width of the write operation in bytes.
 */
void vm_bus_dispatch_write(enum vm_bus_type type, uint64_t addr, uint64_t val,
			   uint8_t size)
{
	struct vm_bus_region *pos;
	struct list_head *list;
	uint64_t offset;

	list = (type == VM_BUS_PIO) ? &pio_regions : &mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (addr >= pos->base && addr < pos->base + pos->size) {
			offset = addr - pos->base;

			/* Strict boundary enforcement */
			if (offset + size > pos->size) {
				pr_warn("cross-boundary write intercepted at offset 0x%lx\n",
					offset);
				return;
			}

			if (pos->dev->ops->write)
				pos->dev->ops->write(pos->dev, offset, val,
						     size);

			return;
		}
	}
}