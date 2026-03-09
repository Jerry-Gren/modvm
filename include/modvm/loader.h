/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_LOADER_H
#define MODVM_LOADER_H

#include <stdint.h>
#include <modvm/memory.h>

/**
 * loader_load_raw_image - load a flat binary file into guest physical memory
 * @space: the memory space to map the image into
 * @path: host filesystem path to the binary image
 * @gpa: guest physical address where the image should be loaded
 *
 * This function reads a raw binary file directly into the guest's RAM.
 * It is primarily used for loading boot ROMs, BIOS images, or simple
 * flat-binary bare-metal kernels.
 *
 * Return: 0 on success, negative error code on failure.
 */
int loader_load_raw_image(struct vm_memory_space *space, const char *path,
			  uint64_t gpa);

#endif /* MODVM_LOADER_H */