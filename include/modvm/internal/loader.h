/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_INTERNAL_LOADER_H
#define MODVM_INTERNAL_LOADER_H

#include <stdint.h>

struct modvm_mem_space;

/*
 * Loader Internal APIs (Cross-Subsystem)
 *
 * Strictly reserved for specific boot protocol implementations.
 */

int modvm_loader_load_raw(struct modvm_mem_space *space, const char *path,
			  uint64_t gpa);

#endif /* MODVM_INTERNAL_LOADER_H */