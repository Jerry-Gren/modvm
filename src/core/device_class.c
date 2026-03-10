/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>
#include <errno.h>

#include <modvm/core/device.h>
#include <modvm/utils/compiler.h>
#include <modvm/utils/err.h>
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
 * vm_device_create - instantiate a hardware peripheral from its blueprint.
 * @machine: the target machine topological container.
 * @name: the unique string identifier of the requested device class.
 * @pdata: hardwired topological routing data.
 *
 * return: 0 upon successful instantiation, or a negative error code.
 */
int vm_device_create(struct vm_machine *machine, const char *name, void *pdata)
{
	int i;

	if (!machine || !name)
		return -EINVAL;

	for (i = 0; i < nr_device_classes; i++) {
		if (strcmp(device_classes[i]->name, name) == 0) {
			if (device_classes[i]->instantiate)
				return device_classes[i]->instantiate(machine,
								      pdata);
			return -ENOTSUP;
		}
	}

	pr_err("device class '%s' not found in registry\n", name);
	return -ENOENT;
}