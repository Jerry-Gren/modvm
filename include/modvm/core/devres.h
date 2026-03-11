/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_DEVRES_H
#define MODVM_CORE_DEVRES_H

#include <stddef.h>

struct vm_device;

/**
 * typedef vm_devres_release_t - callback invoked when a resource is freed
 * @dev: the device that owns the resource
 * @res: pointer to the resource data payload
 */
typedef void (*vm_devres_release_t)(struct vm_device *dev, void *res);

void *vm_devres_alloc(vm_devres_release_t release, size_t size);
void vm_devres_add(struct vm_device *dev, void *res);
void vm_devres_free(struct vm_device *dev, void *res);

void *vm_devm_malloc(struct vm_device *dev, size_t size);
void *vm_devm_zalloc(struct vm_device *dev, size_t size);

int vm_devm_add_action(struct vm_device *dev, void (*action)(void *),
		       void *data);

void vm_devres_release_all(struct vm_device *dev);

#endif /* MODVM_CORE_DEVRES_H */