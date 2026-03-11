/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_DEVRES_H
#define MODVM_CORE_DEVRES_H

#include <stddef.h>
#include <modvm/core/res_pool.h>
#include <modvm/core/device.h>

typedef void (*vm_devres_release_t)(void *owner, void *res);

void *vm_devres_alloc(vm_devres_release_t release, size_t size);
void vm_devres_add(struct vm_device *dev, void *res);
void vm_devres_free(struct vm_device *dev, void *res);

void *vm_devm_malloc(struct vm_device *dev, size_t size);
void *vm_devm_zalloc(struct vm_device *dev, size_t size);
char *vm_devm_strdup(struct vm_device *dev, const char *s);

int __vm_devm_add_action(struct vm_device *dev, void (*action)(void *),
			 void *data);

#define vm_devm_add_action(dev, action, data)                               \
	({                                                                  \
		void (*__action_checker)(__typeof__(data)) = (action);      \
		__vm_devm_add_action(                                       \
			(dev), (void (*)(void *))__action_checker, (data)); \
	})

void vm_devres_release_all(struct vm_device *dev);

#endif /* MODVM_CORE_DEVRES_H */