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

static const struct vm_device_class *device_class_registry[MAX_DEVICE_CLASSES];
static int registered_device_class_count = 0;

void vm_device_class_register(const struct vm_device_class *device_class)
{
	if (registered_device_class_count < MAX_DEVICE_CLASSES) {
		device_class_registry[registered_device_class_count++] =
			device_class;
	}
}

/**
 * vm_device_create - instantiate a hardware peripheral from its class blueprint
 * @machine: the target machine topological container
 * @name: the unique string identifier of the requested device class
 * @platform_data: hardwired topological routing data
 *
 * return: 0 upon successful instantiation, or a negative error code.
 */
int vm_device_create(struct vm_machine *machine, const char *name,
		     void *platform_data)
{
	int index;

	if (!machine || !name)
		return -EINVAL;

	for (index = 0; index < registered_device_class_count; index++) {
		if (strcmp(device_class_registry[index]->name, name) == 0) {
			if (device_class_registry[index]->instantiate)
				return device_class_registry[index]->instantiate(
					machine, platform_data);
			return -ENOTSUP;
		}
	}

	pr_err("device class '%s' not found in registry\n", name);
	return -ENOENT;
}