/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <modvm/core/device.h>
#include <modvm/core/devres.h>
#include <modvm/utils/compiler.h>
#include <modvm/utils/log.h>

#undef pr_fmt
#define pr_fmt(fmt) "device_class: " fmt

#define MAX_DEVICE_CLASSES 64

static const struct vm_device_class *device_classes[MAX_DEVICE_CLASSES];
static int nr_device_classes = 0;

/**
 * vm_device_class_register - register a new device blueprint.
 * @cls: the device class to register.
 */
void vm_device_class_register(const struct vm_device_class *cls)
{
	if (nr_device_classes < MAX_DEVICE_CLASSES)
		device_classes[nr_device_classes++] = cls;
}

/**
 * vm_device_alloc - allocate a lifeless device shell.
 * @machine: the parent machine topology.
 * @name: the identifier of the requested device class.
 *
 * return: a pointer to the empty device shell, or NULL on failure.
 */
struct vm_device *vm_device_alloc(struct vm_machine *machine, const char *name)
{
	struct vm_device *dev;
	const struct vm_device_class *cls = NULL;
	int i;

	if (!machine || !name)
		return NULL;

	for (i = 0; i < nr_device_classes; i++) {
		if (strcmp(device_classes[i]->name, name) == 0) {
			cls = device_classes[i];
			break;
		}
	}

	if (!cls) {
		pr_err("device class '%s' not found\n", name);
		return NULL;
	}

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	dev->cls = cls;
	dev->name = cls->name;
	dev->machine = machine;
	INIT_LIST_HEAD(&dev->node);
	INIT_LIST_HEAD(&dev->devres_head);

	return dev;
}

/**
 * vm_device_add - awaken the device and attach it to the system.
 * @dev: the device shell to instantiate.
 * @pdata: hardware routing data from the motherboard.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_device_add(struct vm_device *dev, void *pdata)
{
	int ret;

	if (!dev || !dev->cls)
		return -EINVAL;

	if (dev->cls->instantiate) {
		ret = dev->cls->instantiate(dev, pdata);
		if (ret < 0)
			return ret;
	}

	list_add_tail(&dev->node, &dev->machine->devices);
	return 0;
}

/**
 * vm_device_put - drop the device and release all managed resources.
 * @dev: the device to destroy.
 */
void vm_device_put(struct vm_device *dev)
{
	if (!dev)
		return;

	vm_devres_release_all(dev);
	free(dev);
}