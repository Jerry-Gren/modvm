/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_INTERNAL_H
#define MODVM_CORE_INTERNAL_H

#include <modvm/core/memory.h>
#include <modvm/core/res_pool.h>

/*
 * Core Subsystem Internal APIs
 * 
 * These functions are strictly private to the modvm core engine.
 * Device models, board topologies, and loaders MUST NOT use them directly.
 */

/* -- Memory Subsystem Internal -- */
int modvm_mem_region_add(struct modvm_mem_space *space, uint64_t gpa,
			 size_t size, uint32_t flags);

/* -- Resource Pool Subsystem Internal -- */
void *modvm_res_alloc(modvm_res_release_t release, size_t size);
void modvm_res_add(struct modvm_res_pool *pool, void *res);
void modvm_res_free(struct modvm_res_pool *pool, void *res);

#endif /* MODVM_CORE_INTERNAL_H */