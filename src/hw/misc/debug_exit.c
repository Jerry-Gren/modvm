/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/bus.h>
#include <modvm/core/machine.h>
#include <modvm/core/devres.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>

#define DEBUG_EXIT_BASE_PORT 0x500

#undef pr_fmt
#define pr_fmt(fmt) "debug_exit: " fmt

struct debug_exit_ctx {
	struct vm_machine *machine;
};

static void debug_exit_write(struct vm_device *dev, uint64_t offset,
			     uint64_t val, uint8_t size)
{
	struct debug_exit_ctx *ctx = dev->priv;

	(void)offset;
	(void)size;

	pr_info("guest requested power off (exit code 0x%lx)\n", val);
	vm_machine_request_shutdown(ctx->machine);
}

static const struct vm_device_ops debug_exit_ops = {
	.write = debug_exit_write,
};

/**
 * debug_exit_instantiate - allocate and register the debug exit peripheral
 * @dev: the abstract device object assigned by the core framework
 * @pdata: immutable routing configuration (unused)
 *
 * return: 0 upon successful initialization, or a negative error code.
 */
static int debug_exit_instantiate(struct vm_device *dev, void *pdata)
{
	struct debug_exit_ctx *ctx;
	int ret;

	(void)pdata;

	ctx = vm_devm_zalloc(dev, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	/* dev->machine is already populated by vm_device_create */
	ctx->machine = dev->machine;

	dev->name = "debug-exit";
	dev->ops = &debug_exit_ops;
	dev->priv = ctx;

	ret = vm_bus_register_region(VM_BUS_PIO, DEBUG_EXIT_BASE_PORT, 1, dev);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct vm_device_class debug_exit_class = {
	.name = "debug-exit",
	.instantiate = debug_exit_instantiate,
};

static void __attribute__((constructor)) register_debug_exit_class(void)
{
	vm_device_class_register(&debug_exit_class);
}