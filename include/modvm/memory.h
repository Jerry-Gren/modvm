/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_MEMORY_H
#define MODVM_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <modvm/list.h>

#define VM_MEM_F_READONLY (1U << 0)
#define VM_MEM_F_EXEC (1U << 1)

struct vm_memory_space;
struct vm_memory_region;

/**
 * typedef vm_mem_map_fn - arch-specific callback for hypervisor memory mapping
 * @space: the memory space context
 * @region: the newly allocated region that needs hypervisor mapping
 * @opaque: architecture-specific context (e.g., struct kvm_vm or whpx_partition)
 *
 * This hook bridges the arch-agnostic core and the hypervisor backend.
 */
typedef int (*vm_mem_map_fn)(struct vm_memory_space *space,
			     struct vm_memory_region *region, void *opaque);

struct vm_memory_region {
	struct list_head node;
	uint64_t gpa;
	uint64_t size;
	void *hva;
	uint32_t flags;
};

struct vm_memory_space {
	struct list_head regions;
	uint64_t total_ram;
	size_t host_page_size;

	/* Arch-specific mapping hooks */
	vm_mem_map_fn arch_map_cb;
	void *arch_opaque;
};

int vm_memory_space_init(struct vm_memory_space *space, vm_mem_map_fn map_cb,
			 void *opaque);
void vm_memory_space_destroy(struct vm_memory_space *space);

int vm_memory_region_add(struct vm_memory_space *space, uint64_t gpa,
			 uint64_t size, uint32_t flags);

void *vm_memory_gpa_to_hva(struct vm_memory_space *space, uint64_t gpa);

#endif /* MODVM_MEMORY_H */