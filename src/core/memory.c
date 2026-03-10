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
 * vm_mem_space_init - initialize a fresh physical memory controller.
 * @space: the memory space context to initialize.
 * @map_cb: function to invoke when a new memory region needs hardware mapping.
 * @data: private context data passed to the map_cb.
 *
 * return: 0 on success, or a negative error code on invalid arguments.
 */
int vm_mem_space_init(struct vm_mem_space *space, vm_mem_map_cb_t map_cb,
		      void *data)
{
	if (WARN_ON(!space))
		return -EINVAL;

	INIT_LIST_HEAD(&space->regions);
	space->total_ram = 0;

	space->host_page_size = os_page_size();
	space->map_cb = map_cb;
	space->map_data = data;

	pr_debug("initialized memory space, host page size: %zu bytes\n",
		 space->host_page_size);

	return 0;
}

static bool is_overlap(uint64_t base1, size_t size1, uint64_t base2,
		       size_t size2)
{
	return (base1 < base2 + size2) && (base2 < base1 + size1);
}

/**
 * vm_mem_region_add - register a contiguous physical memory bank.
 * @space: the memory space to attach this region to.
 * @gpa: the starting address as seen by the virtual processor.
 * @size: total capacity of the memory bank in bytes.
 * @flags: bitmask governing read/write/execute permissions.
 *
 * Allocates host backing memory and registers the mapping with the
 * architecture-specific hypervisor backend. Ensures no address overlap
 * occurs within the existing topology.
 *
 * return: 0 on success, negative error code on overlap or failure.
 */
int vm_mem_region_add(struct vm_mem_space *space, uint64_t gpa, size_t size,
		      uint32_t flags)
{
	struct vm_mem_region *reg;
	struct vm_mem_region *pos;
	int ret;

	if (WARN_ON(!space || size == 0))
		return -EINVAL;

	if (WARN_ON(UINT64_MAX - gpa < size)) {
		pr_err("memory region 0x%lx + size 0x%zx wraps around\n", gpa,
		       size);
		return -EOVERFLOW;
	}

	if (WARN_ON(gpa % space->host_page_size != 0 ||
		    size % space->host_page_size != 0)) {
		pr_err("region (gpa 0x%lx, size 0x%zx) not aligned to %zu bytes\n",
		       gpa, size, space->host_page_size);
		return -EINVAL;
	}

	list_for_each_entry(pos, &space->regions, node)
	{
		if (is_overlap(gpa, size, pos->gpa, pos->size)) {
			pr_err("overlap detected at gpa 0x%lx\n", gpa);
			return -EBUSY;
		}
	}

	reg = calloc(1, sizeof(*reg));
	if (!reg)
		return -ENOMEM;

	reg->hva = os_page_alloc(size);
	if (IS_ERR(reg->hva)) {
		ret = PTR_ERR(reg->hva);
		pr_err("failed to allocate backing memory for gpa 0x%lx\n",
		       gpa);
		free(reg);
		return ret;
	}

	memset(reg->hva, 0, size);

	reg->gpa = gpa;
	reg->size = size;
	reg->flags = flags;

	if (space->map_cb) {
		ret = space->map_cb(space, reg, space->map_data);
		if (ret != 0) {
			pr_err("hypervisor backend failed to map gpa 0x%lx\n",
			       gpa);
			os_page_free(reg->hva, reg->size);
			free(reg);
			return ret;
		}
	}

	list_add_tail(&reg->node, &space->regions);
	space->total_ram += size;

	pr_info("registered memory region: 0x%08lx - 0x%08lx (%zu MB)\n", gpa,
		gpa + size - 1, size / (1024 * 1024));

	return 0;
}

/**
 * vm_mem_gpa_to_hva - resolve a guest physical address.
 * @space: the memory space containing the topology.
 * @gpa: the absolute physical address requested by the guest.
 *
 * Performs a software page-walk of the registered memory regions.
 *
 * return: the corresponding host virtual address pointer, or NULL if
 * unmapped.
 */
void *vm_mem_gpa_to_hva(struct vm_mem_space *space, uint64_t gpa)
{
	struct vm_mem_region *pos;

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
 * vm_mem_space_destroy - tear down the memory controller and free RAM.
 * @space: the memory space to destroy.
 */
void vm_mem_space_destroy(struct vm_mem_space *space)
{
	struct vm_mem_region *pos, *n;

	if (!space)
		return;

	list_for_each_entry_safe(pos, n, &space->regions, node)
	{
		list_del(&pos->node);
		os_page_free(pos->hva, pos->size);
		free(pos);
	}

	space->total_ram = 0;
}