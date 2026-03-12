/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <modvm/core/modvm.h>
#include <modvm/core/board.h>
#include <modvm/core/device.h>
#include <modvm/core/devm.h>
#include <modvm/core/memory.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>
#include <modvm/core/chardev.h>
#include <modvm/core/irq.h>
#include <modvm/hw/serial.h>
#include <modvm/backends/char/stdio.h>
#include <modvm/arch/x86/regs.h>

#undef pr_fmt
#define pr_fmt(fmt) "test_board: " fmt

static const uint8_t fw_payload[] = { 0xba, 0xf8, 0x03, 0xb0, 0x4f, 0xee,
				      0xb0, 0x4b, 0xee, 0xb0, 0x0d, 0xee,
				      0xb0, 0x0a, 0xee, 0xba, 0x00, 0x05,
				      0xb0, 0x01, 0xee, 0xf4 };

struct mock_irq_route {
	struct modvm_accel *accel;
	uint32_t gsi;
};

static void mock_irq_handler(void *data, int level)
{
	struct mock_irq_route *route = data;
	modvm_accel_set_irq(route->accel, route->gsi, level);
}

static int mock_board_init(struct modvm_ctx *ctx)
{
	struct modvm_device *uart;
	struct modvm_device *exit_dev;
	struct modvm_serial_pdata pdata;
	struct mock_irq_route *route;
	int ret;

	ret = modvm_mem_region_add(&ctx->accel.mem_space, 0x0000, 4096, 0);
	if (ret < 0)
		return ret;

	ret = modvm_accel_setup_irqchip(&ctx->accel);
	if (ret < 0) {
		pr_err("failed to initialize architectural irqchip\n");
		return ret;
	}

	uart = modvm_device_alloc(ctx, "uart-16550a");
	if (!uart)
		return -ENOMEM;

	route = modvm_devm_zalloc(uart, sizeof(*route));
	if (!route) {
		ret = -ENOMEM;
		goto err_uart;
	}

	route->accel = &ctx->accel;
	route->gsi = 4;

	pdata.base = 0x3f8;
	pdata.console = ctx->config.console;
	pdata.irq = modvm_devm_irq_alloc(uart, mock_irq_handler, route);
	if (!pdata.irq) {
		ret = -ENOMEM;
		goto err_uart;
	}

	ret = modvm_device_add(uart, &pdata);
	if (ret < 0) {
		pr_err("failed to probe uart peripheral\n");
		goto err_uart;
	}

	exit_dev = modvm_device_alloc(ctx, "debug-exit");
	if (!exit_dev)
		return -ENOMEM;

	ret = modvm_device_add(exit_dev, NULL);
	if (ret < 0) {
		pr_err("failed to probe debug exit device\n");
		modvm_device_put(exit_dev);
		return ret;
	}

	pr_info("mock hardware topology injected successfully\n");
	return 0;

err_uart:
	modvm_device_put(uart);
	return ret;
}

static int mock_board_reset(struct modvm_ctx *ctx)
{
	struct modvm_x86_sregs sregs;
	struct modvm_x86_regs regs;
	void *hva;
	int ret;

	hva = modvm_mem_gpa_to_hva(&ctx->accel.mem_space, 0x0000);
	if (IS_ERR_OR_NULL(hva)) {
		pr_err("failed to translate gpa 0x0000 for payload injection\n");
		return -EFAULT;
	}

	memcpy(hva, fw_payload, sizeof(fw_payload));
	pr_info("injected %zu bytes of machine code into guest memory\n",
		sizeof(fw_payload));

	ret = modvm_vcpu_get_regs(ctx->vcpus[0], MODVM_REG_CLASS_X86_SREGS,
				  &sregs, sizeof(sregs));
	if (ret < 0)
		return ret;

	sregs.cs.selector = 0x0000;
	sregs.cs.base = 0x00000000;

	ret = modvm_vcpu_set_regs(ctx->vcpus[0], MODVM_REG_CLASS_X86_SREGS,
				  &sregs, sizeof(sregs));
	if (ret < 0)
		return ret;

	memset(&regs, 0, sizeof(regs));
	regs.rip = 0x0000;
	regs.rflags = 0x02;

	ret = modvm_vcpu_set_regs(ctx->vcpus[0], MODVM_REG_CLASS_X86_GPR, &regs,
				  sizeof(regs));
	if (ret < 0)
		return ret;

	return 0;
}

static const struct modvm_board_ops mock_ops = {
	.init = mock_board_init,
	.reset = mock_board_reset,
};

static const struct modvm_board mock_board = {
	.name = "mock",
	.desc = "Mock machine for integration testing",
	.ops = &mock_ops,
};

static void test_machine_lifecycle(void)
{
	struct modvm_ctx vm;
	struct modvm_chardev *console;
	int ret;

	console = modvm_chardev_stdio_create();
	if (WARN_ON(!console))
		modvm_panic("failed to create console backend\n");

	struct modvm_config cfg = {
		.accel_name = "kvm",
		.ram_base = 0x0000,
		.ram_size = 4096,
		.nr_vcpus = 1,
		.loader_name = NULL,
		.loader_opts = NULL,
		.board = &mock_board,
		.console = console,
	};

	pr_info("initiating motherboard initialization sequence\n");

	ret = modvm_init(&vm, &cfg);
	if (WARN_ON(ret < 0))
		modvm_panic("machine assembly failed\n");

	pr_info("igniting processor cores\n");

	ret = modvm_run(&vm);
	if (WARN_ON(ret < 0))
		modvm_panic("hypervisor runtime encountered fatal error\n");

	pr_info("tearing down virtualization context\n");
	modvm_destroy(&vm);
	modvm_chardev_stdio_destroy(console);
}

int main(void)
{
	modvm_log_initialize();
	pr_info("Initiating ModVM board integration test\n");
	test_machine_lifecycle();
	pr_info("SUCCESS: machine architecture test concluded successfully\n");
	modvm_log_destroy();
	return 0;
}