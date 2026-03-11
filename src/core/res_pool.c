/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include <modvm/core/res_pool.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/container_of.h>

struct res_node {
	struct list_head node;
	vm_res_release_t release;
	size_t size;
	unsigned long long __padding;
	uint8_t data[];
};

static struct res_node *to_res_node(void *data)
{
	return container_of(data, struct res_node, data);
}

/**
 * vm_res_pool_init - initialize a generic resource pool
 * @pool: the resource pool to initialize
 * @owner: the logical owner passed to subsequent release callbacks
 */
void vm_res_pool_init(struct vm_res_pool *pool, void *owner)
{
	INIT_LIST_HEAD(&pool->resources);
	pool->owner = owner;
}

/**
 * vm_res_alloc - allocate a standalone resource node
 * @release: callback invoked upon pool destruction
 * @size: size of the resource payload in bytes
 *
 * return: pointer to the resource payload, or NULL on failure.
 */
void *vm_res_alloc(vm_res_release_t release, size_t size)
{
	struct res_node *node;

	if (WARN_ON(size == 0))
		return NULL;

	node = calloc(1, sizeof(*node) + size);
	if (!node)
		return NULL;

	INIT_LIST_HEAD(&node->node);
	node->release = release;
	node->size = size;

	return node->data;
}

/**
 * vm_res_add - attach an allocated resource to a pool
 * @pool: the target resource pool
 * @res: the resource payload pointer previously returned by vm_res_alloc
 */
void vm_res_add(struct vm_res_pool *pool, void *res)
{
	struct res_node *node = to_res_node(res);

	if (WARN_ON(!pool || !res))
		return;

	list_add_tail(&node->node, &pool->resources);
}

/**
 * vm_res_free - explicitly free a resource early
 * @pool: the pool owning the resource
 * @res: the resource payload pointer
 */
void vm_res_free(struct vm_res_pool *pool, void *res)
{
	struct res_node *node;

	if (!res)
		return;

	node = to_res_node(res);
	list_del(&node->node);

	if (node->release)
		node->release(pool->owner, res);

	free(node);
}

/**
 * vm_res_release_all - invoke callbacks and free all resources in the pool
 * @pool: the resource pool being dismantled
 *
 * Iterates safely in reverse order to unwind dependencies cleanly.
 */
void vm_res_release_all(struct vm_res_pool *pool)
{
	struct res_node *node, *n;

	if (!pool)
		return;

	list_for_each_entry_safe_reverse(node, n, &pool->resources, node)
	{
		list_del(&node->node);
		if (node->release)
			node->release(pool->owner, node->data);
		free(node);
	}
}

struct res_action_data {
	void (*action)(void *);
	void *data;
};

static void res_action_release(void *owner, void *res)
{
	struct res_action_data *act = res;
	(void)owner;

	if (act->action)
		act->action(act->data);
}

/**
 * vm_res_add_action - queue a custom cleanup action to the pool
 * @pool: the resource pool to bind the action to
 * @action: the callback function to execute upon destruction
 * @data: contextual argument passed to the callback
 *
 * Return: 0 on success, or a negative error code.
 */
int __vm_res_add_action(struct vm_res_pool *pool, void (*action)(void *),
			void *data)
{
	struct res_action_data *act;

	act = vm_res_alloc(res_action_release, sizeof(*act));
	if (!act)
		return -ENOMEM;

	act->action = action;
	act->data = data;
	vm_res_add(pool, act);

	return 0;
}