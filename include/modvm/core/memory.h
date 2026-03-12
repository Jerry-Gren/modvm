/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_MEMORY_H
#define MODVM_CORE_MEMORY_H

#include <stdint.h>
#include <modvm/utils/list.h>

#define MODVM_MEM_READONLY (1U << 0)
#define MODVM_MEM_EXEC (1U << 1)

struct modvm_mem_space;
struct modvm_mem_region;

/**
 * typedef modvm_mem_map_cb_t - architecture-specific callback for hypervisor memory mapping
 * @space: the memory space context
 * @region: the newly allocated region requiring hypervisor mapping
 * @data: architecture-specific context passed during initialization
 */
typedef int (*modvm_mem_map_cb_t)(struct modvm_mem_space *space,
				  struct modvm_mem_region *region, void *data);

/**
 * struct modvm_mem_region - contiguous block of guest physical memory
 * @node: linked list node for memory space iterations
 * @gpa: guest physical address
 * @size: size of the memory block in bytes
 * @hva: host virtual address backing this region
 * @flags: access permissions and memory traits
 */
struct modvm_mem_region {
	struct list_head node;
	uint64_t gpa;
	size_t size;
	void *hva;
	uint32_t flags;
};

/**
 * struct modvm_mem_space - physical memory controller for a virtual machine
 * @regions: list of registered memory regions
 * @total_ram: aggregate size of available memory in bytes
 * @host_page_size: native page size of the underlying operating system
 * @map_cb: hook to notify hypervisor of new mappings
 * @map_data: private context for the mapping hook
 */
struct modvm_mem_space {
	struct list_head regions;
	size_t total_ram;
	size_t host_page_size;

	modvm_mem_map_cb_t map_cb;
	void *map_data;
};

int modvm_mem_space_init(struct modvm_mem_space *space,
			 modvm_mem_map_cb_t map_cb, void *data);
void modvm_mem_space_destroy(struct modvm_mem_space *space);
int modvm_mem_region_add(struct modvm_mem_space *space, uint64_t gpa,
			 size_t size, uint32_t flags);
void *modvm_mem_gpa_to_hva(struct modvm_mem_space *space, uint64_t gpa);

#endif /* MODVM_CORE_MEMORY_H */