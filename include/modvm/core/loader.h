/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_LOADER_H
#define MODVM_CORE_LOADER_H

#include <stdint.h>
#include <modvm/core/memory.h>

/**
 * vm_loader_load_raw - load a flat binary file into guest memory.
 * @space: the memory space to map the image into.
 * @path: host filesystem path to the binary image.
 * @gpa: guest physical address where the image should be loaded.
 *
 * Reads a raw binary file directly into the guest physical memory via
 * the host virtual address translation. Used for bootstrapping firmware.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_loader_load_raw(struct vm_mem_space *space, const char *path,
		       uint64_t gpa);

#endif /* MODVM_CORE_LOADER_H */