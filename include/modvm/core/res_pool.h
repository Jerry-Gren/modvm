/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_RES_POOL_H
#define MODVM_CORE_RES_POOL_H

#include <stddef.h>
#include <modvm/utils/list.h>

/**
 * typedef vm_res_release_t - callback invoked when a tracked resource is freed
 * @owner: opaque pointer to the entity owning the resource pool
 * @res: pointer to the resource data payload
 */
typedef void (*vm_res_release_t)(void *owner, void *res);

/**
 * struct vm_res_pool - generic resource management container
 * @resources: list head tracking all allocated resource nodes
 * @owner: the logical owner passed to release callbacks
 *
 * Embed this structure into domain-specific objects to grant them
 * automated resource tracking and deterministic teardown capabilities.
 */
struct vm_res_pool {
	struct list_head resources;
	void *owner;
};

void vm_res_pool_init(struct vm_res_pool *pool, void *owner);
void *vm_res_alloc(vm_res_release_t release, size_t size);
void vm_res_add(struct vm_res_pool *pool, void *res);
void vm_res_free(struct vm_res_pool *pool, void *res);
void vm_res_release_all(struct vm_res_pool *pool);

int __vm_res_add_action(struct vm_res_pool *pool, void (*action)(void *),
			void *data);

/**
 * vm_res_add_action - queue a custom cleanup action to the pool (Type Safe)
 */
#define vm_res_add_action(pool, action, data)                                \
	({                                                                   \
		void (*__action_checker)(__typeof__(data)) = (action);       \
		__vm_res_add_action(                                         \
			(pool), (void (*)(void *))__action_checker, (data)); \
	})

#endif /* MODVM_CORE_RES_POOL_H */