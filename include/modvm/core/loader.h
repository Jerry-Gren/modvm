/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_LOADER_H
#define MODVM_CORE_LOADER_H

#include <stdint.h>
#include <modvm/core/memory.h>

struct vm_memory_space;

/**
 * vm_loader_load_raw_image - load a flat binary file into guest memory
 * @memory_space: the memory space to map the image into
 * @file_path: host filesystem path to the binary image
 * @guest_physical_address: guest physical address where the image should be loaded
 *
 * This utility reads a raw binary file directly into the guest physical
 * memory via the host virtual address translation. It is primarily used
 * for bootstrapping minimal bare-metal kernels or firmware blobs.
 */
int vm_loader_load_raw_image(struct vm_memory_space *memory_space,
			     const char *file_path,
			     uint64_t guest_physical_address);

#endif /* MODVM_CORE_LOADER_H */