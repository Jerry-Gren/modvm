/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <modvm/core/modvm.h>
#include <modvm/core/device.h>
#include <modvm/core/devm.h>
#include <modvm/utils/compiler.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>

#undef pr_fmt
#define pr_fmt(fmt) "device: " fmt

#define MAX_DEVICE_CLASSES 64

static const struct modvm_device_class *device_classes[MAX_DEVICE_CLASSES];
static int nr_device_classes = 0;

/**
 * modvm_device_class_register - register a new hardware blueprint
 * @cls: the device class definition to register
 */
void modvm_device_class_register(const struct modvm_device_class *cls)
{
	if (WARN_ON(!cls || !cls->name))
		return;

	if (WARN_ON(nr_device_classes >= MAX_DEVICE_CLASSES)) {
		pr_err("maximum device registry capacity exceeded\n");
		return;
	}

	device_classes[nr_device_classes++] = cls;
}

/**
 * modvm_device_alloc - allocate a lifeless device shell
 * @ctx: the parent machine context
 * @name: the string identifier of the requested device class
 *
 * Scans the static registry for the specified device blueprint and
 * initializes the base structure, including its private resource pool.
 *
 * Return: pointer to the initialized device shell, or NULL on failure.
 */
struct modvm_device *modvm_device_alloc(struct modvm_ctx *ctx, const char *name)
{
	struct modvm_device *dev;
	const struct modvm_device_class *cls = NULL;
	int i;

	if (WARN_ON(!ctx || !name))
		return NULL;

	for (i = 0; i < nr_device_classes; i++) {
		if (strcmp(device_classes[i]->name, name) == 0) {
			cls = device_classes[i];
			break;
		}
	}

	if (!cls) {
		pr_err("device class '%s' not found in registry\n", name);
		return NULL;
	}

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	dev->cls = cls;
	dev->name = cls->name;
	dev->ctx = ctx;

	INIT_LIST_HEAD(&dev->node);
	modvm_res_pool_init(&dev->devm_pool, dev);

	return dev;
}

/**
 * modvm_device_add - awaken the device and attach it to the system topology
 * @dev: the device shell to instantiate
 * @pdata: hardware routing data from the motherboard (platform data)
 *
 * Invokes the specific initialization routine defined by the device class.
 * Upon success, the device is linked into the global machine context.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_device_add(struct modvm_device *dev, void *pdata)
{
	int ret;

	if (WARN_ON(!dev || !dev->cls || !dev->ctx))
		return -EINVAL;

	if (dev->cls->instantiate) {
		ret = dev->cls->instantiate(dev, pdata);
		if (ret < 0)
			return ret;
	}

	list_add_tail(&dev->node, &dev->ctx->devices);
	return 0;
}

/**
 * modvm_device_put - drop the device and release all associated resources
 * @dev: the device to destroy
 *
 * Triggers the devm release sequence which safely unwinds all allocations,
 * interrupts, and bus mappings requested by this device.
 */
void modvm_device_put(struct modvm_device *dev)
{
	if (WARN_ON(!dev))
		return;

	modvm_devm_release_all(dev);
	free(dev);
}