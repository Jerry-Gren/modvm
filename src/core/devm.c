/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/devm.h>
#include <modvm/core/device.h>
#include <modvm/core/res_pool.h>
#include <modvm/utils/bug.h>

#include "internal.h"

struct devm_action_data {
	void (*action)(void *);
	void *data;
};

static void modvm_devm_memory_release(void *owner, void *res)
{
	(void)owner;
	(void)res;
}

static void modvm_devm_action_release(void *owner, void *res)
{
	struct devm_action_data *act = res;

	(void)owner;
	if (act->action)
		act->action(act->data);
}

/**
 * modvm_devm_malloc - allocate device-managed memory
 * @dev: the peripheral device owning the allocation
 * @size: amount of memory to allocate in bytes
 *
 * Return: pointer to allocated memory, or NULL on failure.
 */
void *modvm_devm_malloc(struct modvm_device *dev, size_t size)
{
	void *res;

	if (WARN_ON(!dev || size == 0))
		return NULL;

	res = modvm_res_alloc(modvm_devm_memory_release, size);
	if (res)
		modvm_res_add(&dev->devm_pool, res);

	return res;
}

/**
 * modvm_devm_zalloc - allocate zero-initialized device-managed memory
 * @dev: the peripheral device owning the allocation
 * @size: amount of memory to allocate in bytes
 *
 * Return: pointer to zeroed memory, or NULL on failure.
 */
void *modvm_devm_zalloc(struct modvm_device *dev, size_t size)
{
	void *res = modvm_devm_malloc(dev, size);

	if (res)
		memset(res, 0, size);

	return res;
}

/**
 * modvm_devm_strdup - allocate a device-managed duplicate of a string
 * @dev: the peripheral device
 * @s: the null-terminated string to duplicate
 *
 * Return: pointer to the duplicated string, or NULL on failure.
 */
char *modvm_devm_strdup(struct modvm_device *dev, const char *s)
{
	size_t size;
	char *res;

	if (WARN_ON(!dev || !s))
		return NULL;

	size = strlen(s) + 1;
	res = modvm_devm_malloc(dev, size);
	if (res)
		memcpy(res, s, size);

	return res;
}

/**
 * __modvm_devm_add_action - queue an untyped cleanup action to the device
 * @dev: the peripheral device
 * @action: the raw callback function
 * @data: the raw contextual argument
 *
 * Return: 0 on success, or a negative error code.
 */
int __modvm_devm_add_action(struct modvm_device *dev, void (*action)(void *),
			    void *data)
{
	struct devm_action_data *act;

	if (WARN_ON(!dev || !action))
		return -EINVAL;

	act = modvm_res_alloc(modvm_devm_action_release, sizeof(*act));
	if (!act)
		return -ENOMEM;

	act->action = action;
	act->data = data;
	modvm_res_add(&dev->devm_pool, act);

	return 0;
}

/**
 * modvm_devm_release_all - dispatch all registered actions and free memory
 * @dev: the peripheral device to dismantle
 */
void modvm_devm_release_all(struct modvm_device *dev)
{
	if (WARN_ON(!dev))
		return;

	modvm_res_release_all(&dev->devm_pool);
}