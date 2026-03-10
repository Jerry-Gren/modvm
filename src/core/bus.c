/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/bus.h>
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
static LIST_HEAD(port_io_regions);
static LIST_HEAD(memory_mapped_io_regions);

/**
 * vm_bus_register_region - map a device into the system address space
 * @space_type: indicates whether this is port I/O or memory-mapped I/O
 * @base_address: absolute starting address on the system bus
 * @length_bytes: size of the claimed region in bytes
 * @device: the peripheral device owning this region
 *
 * Scans the existing topology for overlapping regions to prevent
 * hardware resource collisions before registering the new mapping.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_bus_register_region(enum vm_bus_space_type space_type,
			   uint64_t base_address, uint64_t length_bytes,
			   struct vm_device *device)
{
	struct vm_bus_region *region;
	struct vm_bus_region *position;
	struct list_head *target_list;

	if (WARN_ON(!device || !device->operations || length_bytes == 0))
		return -EINVAL;

	target_list = (space_type == VM_BUS_SPACE_PORT_IO) ?
			      &port_io_regions :
			      &memory_mapped_io_regions;

	list_for_each_entry(position, target_list, node)
	{
		if (base_address <
			    position->base_address + position->length_bytes &&
		    base_address + length_bytes > position->base_address) {
			pr_err("bus conflict detected at base address 0x%lx\n",
			       base_address);
			return -EBUSY;
		}
	}

	region = calloc(1, sizeof(*region));
	if (!region)
		return -ENOMEM;

	region->device = device;
	region->base_address = base_address;
	region->length_bytes = length_bytes;
	region->space_type = space_type;

	list_add_tail(&region->node, target_list);

	pr_debug("registered '%s' to space %d at 0x%lx\n",
		 device->name ? device->name : "unknown", space_type,
		 base_address);

	return 0;
}

/**
 * vm_bus_dispatch_read - route a read request to the appropriate peripheral
 * @space_type: the target address space (Port I/O or MMIO)
 * @address: the absolute physical address requested by the virtual processor
 * @access_size: the width of the read operation in bytes
 *
 * Resolves the device mapping and translates the absolute bus address
 * into a relative offset for the device driver. Includes strict boundary
 * checking to prevent cross-device or out-of-bounds reads.
 *
 * return: the data returned by the device, or all 1s (~0ULL) for unmapped regions.
 */
uint64_t vm_bus_dispatch_read(enum vm_bus_space_type space_type,
			      uint64_t address, uint8_t access_size)
{
	struct vm_bus_region *position;
	struct list_head *target_list;
	uint64_t offset;

	target_list = (space_type == VM_BUS_SPACE_PORT_IO) ?
			      &port_io_regions :
			      &memory_mapped_io_regions;

	list_for_each_entry(position, target_list, node)
	{
		if (address >= position->base_address &&
		    address < position->base_address + position->length_bytes) {
			offset = address - position->base_address;

			/* Strict boundary enforcement to prevent hypervisor out-of-bounds access */
			if (offset + access_size > position->length_bytes) {
				pr_warn("cross-boundary read intercepted at offset 0x%lx\n",
					offset);
				return ~0ULL;
			}

			if (position->device->operations->read) {
				return position->device->operations->read(
					position->device, offset, access_size);
			}
			return 0;
		}
	}

	/* Simulate floating bus behavior where unmapped lines return high */
	return ~0ULL;
}

/**
 * vm_bus_dispatch_write - route a write request to the appropriate peripheral
 * @space_type: the target address space (Port I/O or MMIO)
 * @address: the absolute physical address targeted by the virtual processor
 * @value: the payload to be written
 * @access_size: the width of the write operation in bytes
 *
 * Includes boundary checks. Writes to unmapped regions or across device
 * boundaries are safely dropped (acting as a bit bucket).
 */
void vm_bus_dispatch_write(enum vm_bus_space_type space_type, uint64_t address,
			   uint64_t value, uint8_t access_size)
{
	struct vm_bus_region *position;
	struct list_head *target_list;
	uint64_t offset;

	target_list = (space_type == VM_BUS_SPACE_PORT_IO) ?
			      &port_io_regions :
			      &memory_mapped_io_regions;

	list_for_each_entry(position, target_list, node)
	{
		if (address >= position->base_address &&
		    address < position->base_address + position->length_bytes) {
			offset = address - position->base_address;

			/* Strict boundary enforcement */
			if (offset + access_size > position->length_bytes) {
				pr_warn("cross-boundary write intercepted at offset 0x%lx\n",
					offset);
				return;
			}

			if (position->device->operations->write) {
				position->device->operations->write(
					position->device, offset, value,
					access_size);
			}
			return;
		}
	}
}