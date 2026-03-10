/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/memory.h>
#include <modvm/os/page.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/err.h>

#undef pr_fmt
#define pr_fmt(fmt) "memory: " fmt

/**
 * vm_memory_space_init - initialize a fresh physical memory controller
 * @space: the memory space context to initialize
 * @map_callback: function to invoke when a new memory region needs hardware mapping
 * @data: private context data passed to the map_callback
 *
 * return: 0 on success, or a negative error code on invalid arguments.
 */
int vm_memory_space_init(struct vm_memory_space *space,
			 vm_memory_map_callback_t map_callback, void *data)
{
	if (WARN_ON(!space))
		return -EINVAL;

	INIT_LIST_HEAD(&space->regions);
	space->total_ram_bytes = 0;

	space->host_page_size_bytes = os_page_get_size_bytes();
	space->map_callback = map_callback;
	space->map_data = data;

	pr_debug("initialized memory space, host page size: %zu bytes\n",
		 space->host_page_size_bytes);

	return 0;
}

static bool is_overlap(uint64_t start_address_1, uint64_t size_1,
		       uint64_t start_address_2, uint64_t size_2)
{
	return (start_address_1 < start_address_2 + size_2) &&
	       (start_address_2 < start_address_1 + size_1);
}

/**
 * vm_memory_region_add - register a contiguous physical memory bank
 * @space: the memory space to attach this region to
 * @guest_physical_address: the starting address as seen by the virtual processor
 * @size_bytes: total capacity of the memory bank
 * @access_flags: bitmask governing read/write/execute permissions
 *
 * Allocates host backing memory and registers the mapping with the
 * architecture-specific hypervisor backend. Ensures no address overlap
 * occurs within the existing topology.
 *
 * return: 0 on success, negative error code on overlap, misalignment, or memory exhaustion.
 */
int vm_memory_region_add(struct vm_memory_space *space,
			 uint64_t guest_physical_address, uint64_t size_bytes,
			 uint32_t access_flags)
{
	struct vm_memory_region *region;
	struct vm_memory_region *position;
	int return_code;

	if (WARN_ON(!space || size_bytes == 0))
		return -EINVAL;

	if (WARN_ON(UINT64_MAX - guest_physical_address < size_bytes)) {
		pr_err("memory region 0x%lx + size 0x%lx wraps around\n",
		       guest_physical_address, size_bytes);
		return -EOVERFLOW;
	}

	if (WARN_ON(guest_physical_address % space->host_page_size_bytes != 0 ||
		    size_bytes % space->host_page_size_bytes != 0)) {
		pr_err("region (address 0x%lx, size 0x%lx) not aligned to %zu bytes\n",
		       guest_physical_address, size_bytes,
		       space->host_page_size_bytes);
		return -EINVAL;
	}

	list_for_each_entry(position, &space->regions, node)
	{
		if (is_overlap(guest_physical_address, size_bytes,
			       position->guest_physical_address,
			       position->size_bytes)) {
			pr_err("overlap detected at address 0x%lx\n",
			       guest_physical_address);
			return -EBUSY;
		}
	}

	region = calloc(1, sizeof(*region));
	if (!region)
		return -ENOMEM;

	region->host_virtual_address = os_page_allocate(size_bytes);
	if (IS_ERR(region->host_virtual_address)) {
		return_code = PTR_ERR(region->host_virtual_address);
		pr_err("failed to allocate backing host memory for address 0x%lx\n",
		       guest_physical_address);
		free(region);
		return return_code;
	}

	memset(region->host_virtual_address, 0, size_bytes);

	region->guest_physical_address = guest_physical_address;
	region->size_bytes = size_bytes;
	region->access_flags = access_flags;

	if (space->map_callback) {
		return_code =
			space->map_callback(space, region, space->map_data);
		if (return_code != 0) {
			pr_err("hypervisor backend failed to map address 0x%lx\n",
			       guest_physical_address);
			os_page_free(region->host_virtual_address,
				     region->size_bytes);
			free(region);
			return return_code;
		}
	}

	list_add_tail(&region->node, &space->regions);
	space->total_ram_bytes += size_bytes;

	pr_info("registered memory region: 0x%08lx - 0x%08lx (%lu MB)\n",
		guest_physical_address, guest_physical_address + size_bytes - 1,
		size_bytes / (1024 * 1024));

	return 0;
}

/**
 * vm_memory_guest_to_host_address - resolve a guest physical address
 * @space: the memory space containing the topology
 * @guest_physical_address: the absolute physical address requested by the guest
 *
 * Performs a software page-walk of the registered memory regions.
 *
 * return: the corresponding host virtual address pointer, or NULL if the
 * guest address points to unmapped physical space (e.g., an MMIO hole).
 */
void *vm_memory_guest_to_host_address(struct vm_memory_space *space,
				      uint64_t guest_physical_address)
{
	struct vm_memory_region *position;

	list_for_each_entry(position, &space->regions, node)
	{
		if (guest_physical_address >=
			    position->guest_physical_address &&
		    guest_physical_address < position->guest_physical_address +
						     position->size_bytes) {
			uint64_t offset = guest_physical_address -
					  position->guest_physical_address;
			return (uint8_t *)position->host_virtual_address +
			       offset;
		}
	}

	return NULL;
}

/**
 * vm_memory_space_destroy - tear down the memory controller and free all backing RAM
 * @space: the memory space to destroy
 */
void vm_memory_space_destroy(struct vm_memory_space *space)
{
	struct vm_memory_region *position;
	struct vm_memory_region *next_position;

	if (!space)
		return;

	list_for_each_entry_safe(position, next_position, &space->regions, node)
	{
		list_del(&position->node);
		os_page_free(position->host_virtual_address,
			     position->size_bytes);
		free(position);
	}

	space->total_ram_bytes = 0;
}