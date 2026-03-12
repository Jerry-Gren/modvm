/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/bus.h>
#include <modvm/core/modvm.h>
#include <modvm/core/devm.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>

#define DEBUG_EXIT_BASE_PORT 0x500

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
 * @pdata: immutable routing configuration (unused)
 *
 * Return: 0 upon successful initialization, or a negative error code.
 */
static int debug_exit_instantiate(struct modvm_device *dev, void *pdata)
{
	struct debug_exit_ctx *priv;
	int ret;

	(void)pdata;

	priv = modvm_devm_zalloc(dev, sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	priv->ctx = dev->ctx;

	dev->ops = &debug_exit_ops;
	dev->priv = priv;

	ret = modvm_bus_register_region(MODVM_BUS_PIO, DEBUG_EXIT_BASE_PORT, 1,
					dev);
	if (ret < 0)
		return ret;

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