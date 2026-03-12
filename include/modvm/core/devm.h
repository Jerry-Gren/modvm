/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_DEVM_H
#define MODVM_CORE_DEVM_H

#include <stddef.h>
#include <modvm/core/device.h>

void *modvm_devm_malloc(struct modvm_device *dev, size_t size);
void *modvm_devm_zalloc(struct modvm_device *dev, size_t size);
char *modvm_devm_strdup(struct modvm_device *dev, const char *s);

int __modvm_devm_add_action(struct modvm_device *dev, void (*action)(void *),
			    void *data);

/**
 * modvm_devm_add_action - queue a custom cleanup action to the device
 * @dev: the device to manage this action's lifecycle
 * @action: the callback function to execute upon destruction
 * @data: contextual argument passed to the callback
 *
 * Utilizes a GCC/Clang statement expression to enforce strict compile-time
 * type checking between the data pointer and the action function signature,
 * effectively eliminating undefined behavior risks from blind void pointer casting.
 */
#define modvm_devm_add_action(dev, action, data)                            \
	({                                                                  \
		void (*__checker)(__typeof__(data)) = (action);             \
		__modvm_devm_add_action((dev), (void (*)(void *))__checker, \
					(data));                            \
	})

void modvm_devm_release_all(struct modvm_device *dev);

#endif /* MODVM_CORE_DEVM_H */