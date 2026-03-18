/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/memory.h>
#include <modvm/os/page.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/err.h>
#include <modvm/utils/compiler.h>

#include "internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "memory: " fmt

/**
 * modvm_mem_space_init - initialize a fresh physical memory controller
 * @space: the memory space context to initialize
 * @map_cb: function invoked when a new memory region needs hardware mapping
 * @unmap_cb: function invoked to tear down hardware mappings
 * @data: private context data passed to the mapping hooks
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_mem_space_init(struct modvm_mem_space *space,
			 modvm_mem_map_cb_t map_cb,
			 modvm_mem_unmap_cb_t unmap_cb, void *data)
{
	if (WARN_ON(!space))
		return -EINVAL;

	INIT_LIST_HEAD(&space->regions);
	space->total_ram = 0;
	space->host_page_size = os_page_size();
	space->map_cb = map_cb;
	space->unmap_cb = unmap_cb;
	space->map_data = data;

	return 0;
}

/**
 * modvm_mem_region_is_overlap - check if two memory ranges overlap
 * @base1: start address of first region
 * @size1: length of first region
 * @base2: start address of second region
 * @size2: length of second region
 *
 * Return: true if overlapping, false otherwise.
 */
static bool modvm_mem_region_is_overlap(uint64_t base1, size_t size1,
					uint64_t base2, size_t size2)
{
	return (base1 < base2 + size2) && (base2 < base1 + size1);
}

/**
 * modvm_mem_region_add - map a contiguous host memory block to guest physics
 * @space: the memory space to attach this region to
 * @gpa: the starting physical address requested by the guest
 * @size: capacity of the memory bank in bytes
 * @flags: bitmask governing read/write/execute permissions
 *
 * Validates alignment and topological overlap before allocating OS-level
 * page-aligned anonymous memory and invoking the hypervisor mapping hook.
 *
 * Return: 0 on success, negative error code on conflict or exhaustion.
 */
int modvm_mem_region_add(struct modvm_mem_space *space, uint64_t gpa,
			 size_t size, uint32_t flags)
{
	struct modvm_mem_region *reg;
	struct modvm_mem_region *pos;
	int ret;

	if (WARN_ON(!space || size == 0))
		return -EINVAL;

	if (UINT64_MAX - gpa < size) {
		pr_err("memory region 0x%lx + size 0x%zx wraps around address limit\n",
		       gpa, size);
		return -EOVERFLOW;
	}

	if (gpa % space->host_page_size != 0 ||
	    size % space->host_page_size != 0) {
		pr_err("region (gpa 0x%lx, size 0x%zx) strictly requires %zu bytes alignment\n",
		       gpa, size, space->host_page_size);
		return -EINVAL;
	}

	list_for_each_entry(pos, &space->regions, node)
	{
		if (modvm_mem_region_is_overlap(gpa, size, pos->gpa,
						pos->size)) {
			pr_err("topology overlap detected at gpa 0x%lx\n", gpa);
			return -EBUSY;
		}
	}

	reg = calloc(1, sizeof(*reg));
	if (!reg)
		return -ENOMEM;

	reg->hva = os_page_alloc(size);
	if (IS_ERR(reg->hva)) {
		ret = PTR_ERR(reg->hva);
		pr_err("failed to allocate host backing memory for gpa 0x%lx\n",
		       gpa);
		free(reg);
		return ret;
	}

	/* 
	 * Don't do this :)
	 * We rely on lazy allocation
	 * 
	 * memset(reg->hva, 0, size);
	 */

	reg->gpa = gpa;
	reg->size = size;
	reg->flags = flags;

	if (space->map_cb) {
		ret = space->map_cb(space, reg, space->map_data);
		if (ret != 0) {
			pr_err("hypervisor backend actively rejected mapping for gpa 0x%lx\n",
			       gpa);
			os_page_free(reg->hva, reg->size);
			free(reg);
			return ret;
		}
	}

	list_add_tail(&reg->node, &space->regions);
	space->total_ram += size;

	pr_debug("mounted hardware ram: 0x%08lx - 0x%08lx (%zu MB)\n", gpa,
		 gpa + size - 1, size / (1024 * 1024));

	return 0;
}

/**
 * modvm_mem_gpa_to_hva - resolve a guest physical address
 * @space: the memory space containing the topology
 * @gpa: the absolute physical address requested
 *
 * Traverses the topology to translate guest physical coordinates into
 * host virtual pointers for direct memory payload manipulation.
 *
 * Return: host virtual address pointer, or NULL if out of bounds.
 */
void *modvm_mem_gpa_to_hva(struct modvm_mem_space *space, uint64_t gpa)
{
	struct modvm_mem_region *pos;

	if (WARN_ON(!space))
		return NULL;

	list_for_each_entry(pos, &space->regions, node)
	{
		if (gpa >= pos->gpa && gpa < pos->gpa + pos->size) {
			uint64_t offset = gpa - pos->gpa;
			return (uint8_t *)pos->hva + offset;
		}
	}

	return NULL;
}

/**
 * modvm_mem_space_destroy - dismantle the physical memory controller
 * @space: the memory space to destroy
 */
void modvm_mem_space_destroy(struct modvm_mem_space *space)
{
	struct modvm_mem_region *pos, *n;

	if (WARN_ON(!space))
		return;

	list_for_each_entry_safe(pos, n, &space->regions, node)
	{
		list_del(&pos->node);

		/* Explicitly instruct the hardware to tear down EPT/NPT mappings */
		if (space->unmap_cb)
			space->unmap_cb(space, pos, space->map_data);

		os_page_free(pos->hva, pos->size);
		free(pos);
	}

	space->total_ram = 0;
}