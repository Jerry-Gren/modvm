/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include <modvm/core/res_pool.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/container_of.h>

struct res_node {
	struct list_head node;
	modvm_res_release_t release;
	size_t size;
	unsigned long long __padding;
	uint8_t data[];
};

static struct res_node *to_res_node(void *data)
{
	return container_of(data, struct res_node, data);
}

/**
 * modvm_res_pool_init - initialize a generic resource pool
 * @pool: the resource pool to initialize
 * @owner: the logical owner passed to subsequent release callbacks
 */
void modvm_res_pool_init(struct modvm_res_pool *pool, void *owner)
{
	if (WARN_ON(!pool))
		return;

	INIT_LIST_HEAD(&pool->resources);
	pool->owner = owner;
}

/**
 * modvm_res_alloc - allocate a standalone resource node
 * @release: callback invoked upon pool destruction
 * @size: size of the resource payload in bytes
 *
 * Allocates a unified memory block containing both the tracking header
 * and the payload data.
 *
 * Return: pointer to the resource payload, or NULL on failure.
 */
void *modvm_res_alloc(modvm_res_release_t release, size_t size)
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
 * modvm_res_add - attach an allocated resource to a pool
 * @pool: the target resource pool
 * @res: the resource payload pointer previously returned by modvm_res_alloc
 */
void modvm_res_add(struct modvm_res_pool *pool, void *res)
{
	struct res_node *node;

	if (WARN_ON(!pool || !res))
		return;

	node = to_res_node(res);
	list_add_tail(&node->node, &pool->resources);
}

/**
 * modvm_res_free - explicitly free a tracked resource early
 * @pool: the pool owning the resource
 * @res: the resource payload pointer
 *
 * Safely detaches the resource from the pool, invokes its release callback
 * if present, and frees the underlying memory block.
 */
void modvm_res_free(struct modvm_res_pool *pool, void *res)
{
	struct res_node *node;

	if (WARN_ON(!pool || !res))
		return;

	node = to_res_node(res);
	list_del(&node->node);

	if (node->release)
		node->release(pool->owner, res);

	free(node);
}

/**
 * modvm_res_release_all - invoke callbacks and free all resources in the pool
 * @pool: the resource pool being dismantled
 *
 * Iterates strictly in reverse order to unwind dependencies cleanly, simulating
 * a stack-like deterministic teardown mechanism.
 */
void modvm_res_release_all(struct modvm_res_pool *pool)
{
	struct res_node *node;
	struct res_node *n;

	if (WARN_ON(!pool))
		return;

	list_for_each_entry_safe_reverse(node, n, &pool->resources, node)
	{
		list_del(&node->node);
		if (node->release)
			node->release(pool->owner, node->data);
		free(node);
	}
}