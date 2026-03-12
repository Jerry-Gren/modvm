/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_CTXM_H
#define MODVM_CORE_CTXM_H

#include <modvm/utils/stddef.h>
#include <modvm/core/modvm.h>

void *modvm_ctxm_malloc(struct modvm_ctx *ctx, size_t size);
void *modvm_ctxm_zalloc(struct modvm_ctx *ctx, size_t size);
char *modvm_ctxm_strdup(struct modvm_ctx *ctx, const char *s);

int __modvm_ctxm_add_action(struct modvm_ctx *ctx, void (*action)(void *),
			    void *data);

/**
 * modvm_ctxm_add_action - queue a custom cleanup action to the context
 * @ctx: the virtual machine context
 * @action: the callback function to execute upon destruction
 * @data: contextual argument passed to the callback
 *
 * Utilizes a GCC/Clang statement expression to enforce strict compile-time
 * type checking between the data pointer and the action function signature,
 * effectively eliminating undefined behavior risks from blind void pointer casting.
 */
#define modvm_ctxm_add_action(ctx, action, data)                            \
	({                                                                  \
		void (*__checker)(__typeof__(data)) = (action);             \
		__modvm_ctxm_add_action((ctx), (void (*)(void *))__checker, \
					(data));                            \
	})

#endif /* MODVM_CORE_CTXM_H */