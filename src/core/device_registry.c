/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>
#include <errno.h>

#include <modvm/device.h>
#include <modvm/compiler.h>
#include <modvm/err.h>
#include <modvm/log.h>

#define MAX_DEVICE_CLASSES 64

static const struct vm_device_class *device_registry[MAX_DEVICE_CLASSES];
static int registered_devices = 0;

void vm_device_class_register(const struct vm_device_class *cls)
{
	if (registered_devices < MAX_DEVICE_CLASSES) {
		device_registry[registered_devices++] = cls;
	}
}

int vm_device_create(struct machine *mach, const char *name,
		     void *platform_data)
{
	int i;

	if (!mach || !name)
		return -EINVAL;

	for (i = 0; i < registered_devices; i++) {
		if (strcmp(device_registry[i]->name, name) == 0) {
			if (device_registry[i]->create)
				return device_registry[i]->create(
					mach, platform_data);
			return -ENOTSUP;
		}
	}

	pr_err("Device class '%s' not found in registry\n", name);
	return -ENOENT;
}