/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_MEMORY_H
#define MODVM_CORE_MEMORY_H

#include <stdint.h>
#include <modvm/utils/list.h>

#define VM_MEM_FLAG_READONLY (1U << 0)
#define VM_MEM_FLAG_EXEC (1U << 1)

struct vm_mem_space;
struct vm_mem_region;

/**
 * typedef vm_mem_map_cb_t - architecture-specific callback for hypervisor memory mapping.
 * @space: the memory space context.
 * @region: the newly allocated region requiring hypervisor mapping.
 * @data: architecture-specific context passed during initialization.
 *
 * Bridges the architecture-agnostic core and the hypervisor backend,
 * allowing hardware page tables to be updated upon region registration.
 */
typedef int (*vm_mem_map_cb_t)(struct vm_mem_space *space,
			       struct vm_mem_region *region, void *data);

/**
 * struct vm_mem_region - contiguous block of guest physical memory.
 * @node: linked list node for memory space iterations.
 * @gpa: guest physical address.
 * @size: size of the memory block in bytes.
 * @hva: host virtual address backing this region.
 * @flags: access permissions and memory traits.
 */
struct vm_mem_region {
	struct list_head node;
	uint64_t gpa;
	size_t size;
	void *hva;
	uint32_t flags;
};

/**
 * struct vm_mem_space - physical memory controller for a virtual machine.
 * @regions: list of registered memory regions.
 * @total_ram: aggregate size of available memory in bytes.
 * @host_page_size: native page size of the underlying operating system.
 * @map_cb: hook to notify hypervisor of new mappings.
 * @map_data: private context for the mapping hook.
 */
struct vm_mem_space {
	struct list_head regions;
	size_t total_ram;
	size_t host_page_size;

	vm_mem_map_cb_t map_cb;
	void *map_data;
};

int vm_mem_space_init(struct vm_mem_space *space, vm_mem_map_cb_t map_cb,
		      void *data);

void vm_mem_space_destroy(struct vm_mem_space *space);

int vm_mem_region_add(struct vm_mem_space *space, uint64_t gpa, size_t size,
		      uint32_t flags);

void *vm_mem_gpa_to_hva(struct vm_mem_space *space, uint64_t gpa);

#endif /* MODVM_CORE_MEMORY_H */