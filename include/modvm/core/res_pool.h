/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_RES_POOL_H
#define MODVM_CORE_RES_POOL_H

#include <modvm/utils/stddef.h>
#include <modvm/utils/list.h>

/**
 * typedef modvm_res_release_t - callback invoked when a tracked resource is freed
 * @owner: opaque pointer to the entity owning the resource pool
 * @res: pointer to the resource data payload
 */
typedef void (*modvm_res_release_t)(void *owner, void *res);

/**
 * struct modvm_res_pool - generic resource management container
 * @resources: list head tracking all allocated resource nodes
 * @owner: the logical owner passed to release callbacks
 *
 * Embed this structure into domain-specific objects to grant them
 * automated resource tracking and deterministic teardown capabilities.
 */
struct modvm_res_pool {
	struct list_head resources;
	void *owner;
};

void modvm_res_pool_init(struct modvm_res_pool *pool, void *owner);
void modvm_res_release_all(struct modvm_res_pool *pool);

#endif /* MODVM_CORE_RES_POOL_H */