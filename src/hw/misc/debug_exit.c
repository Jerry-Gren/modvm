/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/bus.h>
#include <modvm/core/modvm.h>
#include <modvm/core/devm.h>
#include <modvm/hw/misc/debug_exit.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>

#undef pr_fmt
#define pr_fmt(fmt) "debug_exit: " fmt

struct debug_exit_ctx {
	struct modvm_ctx *ctx;
};

static void debug_exit_write(struct modvm_device *dev, uint64_t offset,
			     uint64_t val, uint8_t size)
{
	struct debug_exit_ctx *priv = dev->priv;

	(void)offset;
	(void)size;

	pr_info("guest requested power off (exit code 0x%lx)\n", val);
	modvm_request_shutdown(priv->ctx);
}

static const struct modvm_device_ops debug_exit_ops = {
	.write = debug_exit_write,
};

/**
 * debug_exit_instantiate - allocate and register the debug exit peripheral
 * @dev: the abstract device object assigned by the core framework
 * @pdata: immutable routing configuration
 *
 * Return: 0 upon successful initialization, or a negative error code.
 */
static int debug_exit_instantiate(struct modvm_device *dev, void *pdata)
{
	struct modvm_debug_exit_pdata *plat = pdata;
	struct debug_exit_ctx *priv;
	int ret;

	if (WARN_ON(!plat))
		return -EINVAL;

	priv = modvm_devm_zalloc(dev, sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	priv->ctx = dev->ctx;

	dev->ops = &debug_exit_ops;
	dev->priv = priv;

	ret = modvm_bus_register_region(plat->bus_type, plat->base, 1, dev);
	if (ret < 0)
		return ret;

	pr_info("debug exit device attached to %s bus at 0x%08lx\n",
		plat->bus_type == MODVM_BUS_MMIO ? "mmio" : "pio", plat->base);

	return 0;
}

static const struct modvm_device_class debug_exit_class = {
	.name = "debug-exit",
	.instantiate = debug_exit_instantiate,
};

static void __attribute__((constructor)) register_debug_exit_class(void)
{
	modvm_device_class_register(&debug_exit_class);
}