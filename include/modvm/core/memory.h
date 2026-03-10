/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_MEMORY_H
#define MODVM_CORE_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <modvm/utils/list.h>

#define VM_MEMORY_FLAG_READONLY (1U << 0)
#define VM_MEMORY_FLAG_EXECUTABLE (1U << 1)

struct vm_memory_space;
struct vm_memory_region;

/**
 * typedef vm_memory_map_callback_t - architecture-specific callback for hypervisor memory mapping
 * @space: the memory space context
 * @region: the newly allocated region requiring hypervisor mapping
 * @data: architecture-specific context passed during initialization
 *
 * This hook bridges the architecture-agnostic core and the hypervisor backend,
 * allowing hardware page tables to be updated upon region registration.
 */
typedef int (*vm_memory_map_callback_t)(struct vm_memory_space *space,
					struct vm_memory_region *region,
					void *data);

/**
 * struct vm_memory_region - contiguous block of guest physical memory
 * @node: linked list head for memory space iterations
 * @guest_physical_address: guest physical address
 * @size_bytes: size of the memory block in bytes
 * @host_virtual_address: host virtual address backing this region
 * @access_flags: access permissions and memory traits
 */
struct vm_memory_region {
	struct list_head node;
	uint64_t guest_physical_address;
	uint64_t size_bytes;
	void *host_virtual_address;
	uint32_t access_flags;
};

/**
 * struct vm_memory_space - physical memory controller for a virtual machine
 * @regions: list of registered memory regions
 * @total_ram_bytes: aggregate size of available memory
 * @host_page_size_bytes: native page size of the underlying operating system
 * @map_callback: hook to notify hypervisor of new mappings
 * @map_data: private context for the mapping hook
 */
struct vm_memory_space {
	struct list_head regions;
	uint64_t total_ram_bytes;
	size_t host_page_size_bytes;

	vm_memory_map_callback_t map_callback;
	void *map_data;
};

int vm_memory_space_init(struct vm_memory_space *space,
			 vm_memory_map_callback_t map_callback, void *data);

void vm_memory_space_destroy(struct vm_memory_space *space);

int vm_memory_region_add(struct vm_memory_space *space,
			 uint64_t guest_physical_address, uint64_t size_bytes,
			 uint32_t access_flags);

void *vm_memory_guest_to_host_address(struct vm_memory_space *space,
				      uint64_t guest_physical_address);

#endif /* MODVM_CORE_MEMORY_H */