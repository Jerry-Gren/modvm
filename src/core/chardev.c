/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>
#include <modvm/core/chardev.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/log.h>

#define MAX_CHARDEV_DRIVERS 16
static const struct modvm_chardev_driver *chardev_drivers[MAX_CHARDEV_DRIVERS];
static int nr_chardev_drivers = 0;

void modvm_chardev_driver_register(const struct modvm_chardev_driver *drv)
{
	if (WARN_ON(!drv || !drv->name || !drv->create))
		return;
	if (WARN_ON(nr_chardev_drivers >= MAX_CHARDEV_DRIVERS))
		return;
	chardev_drivers[nr_chardev_drivers++] = drv;
}

struct modvm_chardev *modvm_chardev_create(const char *name, const char *opts)
{
	int i;
	if (WARN_ON(!name))
		return NULL;
	for (i = 0; i < nr_chardev_drivers; i++) {
		if (strcmp(chardev_drivers[i]->name, name) == 0)
			return chardev_drivers[i]->create(opts);
	}
	pr_err("chardev driver '%s' not found\n", name);
	return NULL;
}

void modvm_chardev_release(struct modvm_chardev *dev)
{
	if (!dev)
		return;
	if (dev->ops && dev->ops->release)
		dev->ops->release(dev);
}