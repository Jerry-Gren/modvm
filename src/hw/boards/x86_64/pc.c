/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/board.h>
#include <modvm/core/modvm.h>
#include <modvm/core/device.h>
#include <modvm/core/devm.h>
#include <modvm/core/loader.h>
#include <modvm/hw/serial.h>
#include <modvm/core/irq.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "pc_board: " fmt

/*
 * x86 Architecture Memory Split
 * 0.0 GB - 3.0 GB: Low RAM
 * 3.0 GB - 4.0 GB: PCI MMIO Hole (Contains APIC at 0xFEE00000, BIOS, etc.)
 * 4.0 GB - ...   : High RAM
 */
#define PC_LOW_RAM_MAX 0xC0000000ULL
#define PC_HIGH_RAM_BASE 0x100000000ULL

struct pc_irq_route {
	struct modvm_accel *accel;
	uint32_t gsi;
};

static void pc_irq_handler(void *data, int level)
{
	struct pc_irq_route *route = data;
	modvm_accel_set_irq(route->accel, route->gsi, level);
}

/**
 * pc_board_init - assemble the legacy x86 personal computer topology
 * @ctx: the context instance being constructed
 *
 * Handles intelligent memory splitting around the PCI hole and wires up
 * standard legacy ISA/LPC components.
 *
 * Return: 0 upon successful assembly, or a negative error code.
 */
static int pc_board_init(struct modvm_ctx *ctx)
{
	struct modvm_device *uart;
	struct modvm_device *exit_dev;
	struct modvm_serial_pdata uart_pdata;
	struct pc_irq_route *uart_route;
	uint64_t ram_size = ctx->config.ram_size;
	uint64_t low_ram;
	uint64_t high_ram = 0;
	int ret;

	/* Handle memory splitting to preserve the architectural PCI hole */
	if (ram_size >= PC_LOW_RAM_MAX) {
		low_ram = PC_LOW_RAM_MAX;
		high_ram = ram_size - PC_LOW_RAM_MAX;
	} else {
		low_ram = ram_size;
	}

	ret = modvm_mem_region_add(&ctx->accel.mem_space, 0x00000000, low_ram,
				   MODVM_MEM_EXEC);
	if (ret < 0)
		return ret;

	if (high_ram > 0) {
		ret = modvm_mem_region_add(&ctx->accel.mem_space,
					   PC_HIGH_RAM_BASE, high_ram,
					   MODVM_MEM_EXEC);
		if (ret < 0)
			return ret;
	}

	ret = modvm_accel_setup_irqchip(&ctx->accel);
	if (ret < 0)
		return ret;

	uart = modvm_device_alloc(ctx, "uart-16550a");
	if (!uart)
		return -ENOMEM;

	uart_route = modvm_devm_zalloc(uart, sizeof(*uart_route));
	if (!uart_route) {
		ret = -ENOMEM;
		goto err_uart;
	}

	uart_route->accel = &ctx->accel;
	uart_route->gsi = 4;

	uart_pdata.base = 0x3f8;
	uart_pdata.console = ctx->config.console;
	uart_pdata.irq = modvm_devm_irq_alloc(uart, pc_irq_handler, uart_route);
	if (!uart_pdata.irq) {
		ret = -ENOMEM;
		goto err_uart;
	}

	ret = modvm_device_add(uart, &uart_pdata);
	if (ret < 0)
		goto err_uart;

	exit_dev = modvm_device_alloc(ctx, "debug-exit");
	if (!exit_dev)
		return -ENOMEM;

	ret = modvm_device_add(exit_dev, NULL);
	if (ret < 0) {
		modvm_device_put(exit_dev);
		return ret;
	}

	return 0;

err_uart:
	modvm_device_put(uart);
	return ret;
}

/**
 * pc_board_reset - orchestrate the boot sequence for the PC architecture
 * @ctx: the context instance to reset
 * 
 * Return: 0 on success, or a negative error code.
 */
static int pc_board_reset(struct modvm_ctx *ctx)
{
	int ret;

	/* Delegate entirely to the pluggable loader framework */
	if (ctx->config.loader_name && ctx->config.loader_opts) {
		ret = modvm_loader_execute(ctx, ctx->config.loader_name,
					   ctx->config.loader_opts);
		if (ret < 0) {
			pr_err("board reset failed during firmware handoff\n");
			return ret;
		}
	} else {
		pr_warn("no loader or firmware specified, processor will halt\n");
	}

	return 0;
}

static const struct modvm_board_ops pc_ops = {
	.init = pc_board_init,
	.reset = pc_board_reset,
};

static const struct modvm_board pc_board = {
	.name = "pc",
	.desc = "Standard x86 Personal Computer",
	.ops = &pc_ops,
};

static void __attribute__((constructor)) register_pc_board(void)
{
	modvm_board_register(&pc_board);
}