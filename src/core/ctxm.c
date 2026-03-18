/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/ctxm.h>
#include <modvm/core/res_pool.h>
#include <modvm/utils/bug.h>

#include "internal.h"

struct ctxm_action_data {
	void (*action)(void *);
	void *data;
};

static void modvm_ctxm_memory_release(void *owner, void *res)
{
	(void)owner;
	(void)res;
}

static void modvm_ctxm_action_release(void *owner, void *res)
{
	struct ctxm_action_data *act = res;

	(void)owner;
	if (act->action)
		act->action(act->data);
}

/**
 * modvm_ctxm_malloc - allocate context-managed memory
 * @ctx: the virtual machine context
 * @size: amount of memory to allocate in bytes
 *
 * Return: pointer to allocated memory, or NULL on failure.
 */
void *modvm_ctxm_malloc(struct modvm_ctx *ctx, size_t size)
{
	void *res;

	if (WARN_ON(!ctx || size == 0))
		return NULL;

	res = modvm_res_alloc(modvm_ctxm_memory_release, size);
	if (res)
		modvm_res_add(&ctx->ctxm_pool, res);

	return res;
}

/**
 * modvm_ctxm_zalloc - allocate zero-initialized context-managed memory
 * @ctx: the virtual machine context
 * @size: amount of memory to allocate in bytes
 *
 * Return: pointer to zeroed memory, or NULL on failure.
 */
void *modvm_ctxm_zalloc(struct modvm_ctx *ctx, size_t size)
{
	void *res = modvm_ctxm_malloc(ctx, size);

	if (res)
		memset(res, 0, size);

	return res;
}

/**
 * modvm_ctxm_strdup - allocate a context-managed duplicate of a string
 * @ctx: the virtual machine context
 * @s: the null-terminated string to duplicate
 *
 * Return: pointer to the duplicated string, or NULL on failure.
 */
char *modvm_ctxm_strdup(struct modvm_ctx *ctx, const char *s)
{
	size_t size;
	char *res;

	if (WARN_ON(!ctx || !s))
		return NULL;

	size = strlen(s) + 1;
	res = modvm_ctxm_malloc(ctx, size);
	if (res)
		memcpy(res, s, size);

	return res;
}

/**
 * __modvm_ctxm_add_action - queue an untyped custom cleanup action
 * @ctx: the virtual machine context
 * @action: the raw callback function
 * @data: the raw contextual argument
 *
 * Note: External components should always use the modvm_ctxm_add_action macro
 * to benefit from strict type checking.
 *
 * Return: 0 on success, or a negative error code.
 */
int __modvm_ctxm_add_action(struct modvm_ctx *ctx, void (*action)(void *),
			    void *data)
{
	struct ctxm_action_data *act;

	if (WARN_ON(!ctx || !action))
		return -EINVAL;

	act = modvm_res_alloc(modvm_ctxm_action_release, sizeof(*act));
	if (!act)
		return -ENOMEM;

	act->action = action;
	act->data = data;
	modvm_res_add(&ctx->ctxm_pool, act);

	return 0;
}