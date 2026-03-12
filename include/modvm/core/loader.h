/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_LOADER_H
#define MODVM_CORE_LOADER_H

#include <stdint.h>
#include <modvm/core/memory.h>

int modvm_loader_load_raw(struct modvm_mem_space *space, const char *path,
			  uint64_t gpa);

#endif /* MODVM_CORE_LOADER_H */