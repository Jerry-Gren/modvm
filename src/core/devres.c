/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/devres.h>
#include <modvm/core/device.h>
#include <modvm/core/res_pool.h>

struct devm_action_data {
	void (*action)(void *);
	void *data;
};

void *vm_devres_alloc(vm_devres_release_t release, size_t size)
{
	/* C allows safe casting between identical ABI function signatures */
	return vm_res_alloc((vm_res_release_t)release, size);
}

void vm_devres_add(struct vm_device *dev, void *res)
{
	vm_res_add(&dev->devres_pool, res);
}

void vm_devres_free(struct vm_device *dev, void *res)
{
	vm_res_free(&dev->devres_pool, res);
}

static void devm_memory_release(void *owner, void *res)
{
	(void)owner;
	(void)res;
}

void *vm_devm_malloc(struct vm_device *dev, size_t size)
{
	void *res = vm_res_alloc(devm_memory_release, size);
	if (res)
		vm_res_add(&dev->devres_pool, res);
	return res;
}

void *vm_devm_zalloc(struct vm_device *dev, size_t size)
{
	void *res = vm_devm_malloc(dev, size);
	if (res)
		memset(res, 0, size);
	return res;
}

/**
 * vm_devm_strdup - allocate a managed duplicate of a string
 * @dev: the device to manage this string's lifecycle
 * @s: the string to duplicate
 *
 * return: pointer to the duplicated string, or NULL on failure.
 */
char *vm_devm_strdup(struct vm_device *dev, const char *s)
{
	size_t size;
	char *res;

	if (!s)
		return NULL;

	size = strlen(s) + 1;
	res = vm_devm_malloc(dev, size);
	if (res)
		memcpy(res, s, size);

	return res;
}

static void devm_action_release(void *owner, void *res)
{
	struct devm_action_data *act = res;
	(void)owner;

	if (act->action)
		act->action(act->data);
}

int __vm_devm_add_action(struct vm_device *dev, void (*action)(void *),
			 void *data)
{
	struct devm_action_data *act;

	act = vm_res_alloc(devm_action_release, sizeof(*act));
	if (!act)
		return -ENOMEM;

	act->action = action;
	act->data = data;
	vm_res_add(&dev->devres_pool, act);

	return 0;
}

void vm_devres_release_all(struct vm_device *dev)
{
	vm_res_release_all(&dev->devres_pool);
}