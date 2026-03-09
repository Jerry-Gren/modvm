/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/memory.h>
#include <modvm/os_page.h>
#include <modvm/log.h>
#include <modvm/bug.h>
#include <modvm/err.h>

#undef pr_fmt
#define pr_fmt(fmt) "memory: " fmt

/**
 * vm_memory_space_init - initialize the guest physical memory space
 *
 * Return: 0 on success, or -EINVAL if the context pointer is invalid.
 */
int vm_memory_space_init(struct vm_memory_space *space, vm_mem_map_fn map_cb,
			 void *opaque)
{
	if (WARN_ON(!space))
		return -EINVAL;

	INIT_LIST_HEAD(&space->regions);
	space->total_ram = 0;

	/* Query the host OS for its native page size via the abstraction layer */
	space->host_page_size = os_get_page_size();
	space->arch_map_cb = map_cb;
	space->arch_opaque = opaque;

	pr_debug("Initialized memory space. Host page size: %zu bytes\n",
		 space->host_page_size);

	return 0;
}

/**
 * is_overlap - overflow-safe overlap detection
 */
static bool is_overlap(uint64_t start1, uint64_t size1, uint64_t start2,
		       uint64_t size2)
{
	/* max(start1, start2) < min(end1, end2) */
	return (start1 < start2 + size2) && (start2 < start1 + size1);
}

/**
 * vm_memory_region_add - register a contiguous block of physical memory
 *
 * Return: 0 on success, negative POSIX error code on failure.
 */
int vm_memory_region_add(struct vm_memory_space *space, uint64_t gpa,
			 uint64_t size, uint32_t flags)
{
	struct vm_memory_region *region;
	struct vm_memory_region *pos;
	int ret;

	if (WARN_ON(!space || size == 0))
		return -EINVAL;

	/* Overflow protection */
	if (WARN_ON(UINT64_MAX - gpa < size)) {
		pr_err("Memory region GPA 0x%lx + size 0x%lx wraps around\n",
		       gpa, size);
		return -EOVERFLOW;
	}

	/* Strictly enforce host page alignment */
	if (WARN_ON(gpa % space->host_page_size != 0 ||
		    size % space->host_page_size != 0)) {
		pr_err("Region (GPA 0x%lx, size 0x%lx) not aligned to %zu bytes\n",
		       gpa, size, space->host_page_size);
		return -EINVAL;
	}

	list_for_each_entry(pos, &space->regions, node)
	{
		if (is_overlap(gpa, size, pos->gpa, pos->size)) {
			pr_err("Overlap detected at GPA 0x%lx\n", gpa);
			return -EBUSY;
		}
	}

	region = calloc(1, sizeof(*region));
	if (!region)
		return -ENOMEM;

	/* Rely on the OS abstraction layer and catch pointer errors */
	region->hva = os_alloc_pages(size);
	if (IS_ERR(region->hva)) {
		ret = PTR_ERR(region->hva);
		pr_err("Failed to allocate backing host memory for GPA 0x%lx, err: %d\n",
		       gpa, ret);
		free(region);
		return ret;
	}

	/* Prevent stale host data from leaking into the guest */
	memset(region->hva, 0, size);

	region->gpa = gpa;
	region->size = size;
	region->flags = flags;

	/*
	 * Notify the arch-specific hypervisor backend (KVM/WHPX) to map
	 * this HVA to the GPA in the hardware extended page tables (EPT/NPT).
	 */
	if (space->arch_map_cb) {
		ret = space->arch_map_cb(space, region, space->arch_opaque);
		if (ret != 0) {
			pr_err("Hypervisor backend failed to map GPA 0x%lx, err: %d\n",
			       gpa, ret);
			os_free_pages(region->hva, region->size);
			free(region);
			return ret;
		}
	}

	list_add_tail(&region->node, &space->regions);
	space->total_ram += size;

	pr_info("Registered memory region: GPA 0x%08lx - 0x%08lx (%lu MB)\n",
		gpa, gpa + size - 1, size / (1024 * 1024));

	return 0;
}

/**
 * vm_memory_gpa_to_hva - translate guest physical address (GPA) to host virtual address (HVA)
 *
 * Return: a valid host pointer, or NULL if the GPA is unmapped.
 */
void *vm_memory_gpa_to_hva(struct vm_memory_space *space, uint64_t gpa)
{
	struct vm_memory_region *pos;

	list_for_each_entry(pos, &space->regions, node)
	{
		if (gpa >= pos->gpa && gpa < pos->gpa + pos->size) {
			uint64_t offset = gpa - pos->gpa;
			return (uint8_t *)pos->hva + offset;
		}
	}

	/*
	 * Returning NULL is architecturally correct here. Accessing unmapped
	 * physical memory on real hardware causes a floating bus read or a
	 * machine check exception, which the caller must handle.
	 * 
	 * Don't change it to ERRNO :)
	 */
	return NULL;
}

void vm_memory_space_destroy(struct vm_memory_space *space)
{
	struct vm_memory_region *pos, *n;

	if (!space)
		return;

	list_for_each_entry_safe(pos, n, &space->regions, node)
	{
		list_del(&pos->node);
		os_free_pages(pos->hva, pos->size);
		free(pos);
	}

	space->total_ram = 0;
}