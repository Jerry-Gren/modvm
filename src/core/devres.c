/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/devres.h>
#include <modvm/core/device.h>
#include <modvm/utils/list.h>
#include <modvm/utils/bug.h>

#undef pr_fmt
#define pr_fmt(fmt) "devres: " fmt

struct devres_node {
	struct list_head node;
	vm_devres_release_t release;
	size_t size;
	unsigned long long __padding; /* Ensure natural alignment for payload */
	uint8_t data[];
};

struct devm_action_data {
	void (*action)(void *);
	void *data;
};

static struct devres_node *to_devres_node(void *data)
{
	return container_of(data, struct devres_node, data);
}

/**
 * vm_devres_alloc - allocate a device resource node
 * @release: release callback invoked upon device destruction
 * @size: size of the resource payload
 *
 * Allocates a resource node but does not attach it to any device.
 *
 * return: pointer to the resource payload, or NULL on failure.
 */
void *vm_devres_alloc(vm_devres_release_t release, size_t size)
{
	struct devres_node *node;

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
 * vm_devres_add - attach an allocated resource to a device
 * @dev: the device to attach to
 * @res: the resource payload pointer previously returned by vm_devres_alloc
 */
void vm_devres_add(struct vm_device *dev, void *res)
{
	struct devres_node *node = to_devres_node(res);

	if (WARN_ON(!dev || !res))
		return;

	list_add_tail(&node->node, &dev->devres_head);
}

/**
 * vm_devres_free - explicitly free a device resource early
 * @dev: the device owning the resource
 * @res: the resource payload pointer
 */
void vm_devres_free(struct vm_device *dev, void *res)
{
	struct devres_node *node;

	if (!res)
		return;

	node = to_devres_node(res);
	list_del(&node->node);

	if (node->release)
		node->release(dev, res);

	free(node);
}

static void devm_memory_release(struct vm_device *dev, void *res)
{
	/* * The payload itself does not need complex teardown.
     * The devres core handles freeing the container node.
     */
	(void)dev;
	(void)res;
}

/**
 * vm_devm_malloc - allocate device-managed memory
 * @dev: the device to associate the memory with
 * @size: bytes to allocate
 *
 * return: pointer to allocated memory, or NULL on failure.
 */
void *vm_devm_malloc(struct vm_device *dev, size_t size)
{
	void *res;

	res = vm_devres_alloc(devm_memory_release, size);
	if (res)
		vm_devres_add(dev, res);

	return res;
}

/**
 * vm_devm_zalloc - allocate zeroed device-managed memory
 * @dev: the device to associate the memory with
 * @size: bytes to allocate
 *
 * return: pointer to allocated memory, or NULL on failure.
 */
void *vm_devm_zalloc(struct vm_device *dev, size_t size)
{
	void *res = vm_devm_malloc(dev, size);
	if (res)
		memset(res, 0, size);
	return res;
}

static void devm_action_release(struct vm_device *dev, void *res)
{
	struct devm_action_data *act = res;

	(void)dev;
	if (act->action)
		act->action(act->data);
}

/**
 * vm_devm_add_action - queue a custom cleanup action for device destruction
 * @dev: the device to bind the action to
 * @action: the callback function to execute
 * @data: argument passed to the callback
 *
 * return: 0 on success, or a negative error code.
 */
int vm_devm_add_action(struct vm_device *dev, void (*action)(void *),
		       void *data)
{
	struct devm_action_data *act;

	act = vm_devres_alloc(devm_action_release, sizeof(*act));
	if (!act)
		return -ENOMEM;

	act->action = action;
	act->data = data;
	vm_devres_add(dev, act);

	return 0;
}

/**
 * vm_devres_release_all - invoke callbacks and free all managed resources
 * @dev: the device being dismantled
 *
 * Called automatically by the framework during device destruction.
 */
void vm_devres_release_all(struct vm_device *dev)
{
	struct devres_node *node, *n;

	/* Iterate safely in reverse order to unwind dependencies cleanly */
	list_for_each_entry_safe_reverse(node, n, &dev->devres_head, node)
	{
		list_del(&node->node);
		if (node->release)
			node->release(dev, node->data);
		free(node);
	}
}