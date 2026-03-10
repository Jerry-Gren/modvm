/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/bus.h>
#include <modvm/core/machine.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>

#define DEBUG_EXIT_BASE_PORT 0x500

#undef pr_fmt
#define pr_fmt(fmt) "debug_exit: " fmt

struct debug_exit_ctx {
	struct vm_device dev;
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

static int debug_exit_instantiate(struct vm_machine *machine, void *pdata)
{
	struct debug_exit_ctx *ctx;
	int ret;

	(void)pdata;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	ctx->machine = machine;

	ctx->dev.name = "debug-exit";
	ctx->dev.ops = &debug_exit_ops;
	ctx->dev.priv = ctx;

	ret = vm_bus_register_region(VM_BUS_PIO, DEBUG_EXIT_BASE_PORT, 1,
				     &ctx->dev);
	if (ret < 0) {
		free(ctx);
		return ret;
	}

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