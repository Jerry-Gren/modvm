/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/board.h>
#include <modvm/core/modvm.h>
#include <modvm/core/device.h>
#include <modvm/core/devm.h>
#include <modvm/core/loader.h>
#include <modvm/core/pci.h>
#include <modvm/hw/char/serial.h>
#include <modvm/core/irq.h>
#include <modvm/hw/pci-host/pio_bridge.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_pci.h>
#include <modvm/hw/virtio/virtio_blk.h>
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

static void modvm_hw_pc_irq_handler(void *data, int level)
{
	struct pc_irq_route *route = data;
	modvm_accel_set_irq(route->accel, route->gsi, level);
}

static void modvm_hw_pc_route_pci_irqs(struct modvm_pci_bus *bus)
{
	struct modvm_pci_device *pos;

	list_for_each_entry(pos, &bus->devices, node)
	{
		uint8_t pin;
		uint8_t slot;
		uint8_t irq_line;

		pin = modvm_pci_bus_read_config(bus, pos->devfn,
						PCI_INTERRUPT_PIN, 1);

		if (pin > 0 && pin <= 4) {
			slot = pos->devfn >> 3;
			/* Map Slot/Pin to PIRQA-D, which map to GSIs 10-13 */
			irq_line = 10 + ((slot + pin - 1) % 4);

			modvm_pci_bus_write_config(bus, pos->devfn,
						   PCI_INTERRUPT_LINE, irq_line,
						   1);

			pr_info("firmware routed devfn %u pin %u -> GSI %u\n",
				pos->devfn, pin, irq_line);
		}
	}
}

static int modvm_hw_pc_add_virtio_blk(struct modvm_ctx *ctx,
				      struct modvm_pci_bus *pci_bus,
				      struct modvm_block *blk_backend)
{
	struct virtio_device *vdev_blk;
	struct modvm_device *vpci_dev;
	struct virtio_pci_pdata vpci_pdata;
	int ret;

	vdev_blk = virtio_blk_create(ctx, blk_backend);
	if (!vdev_blk)
		return -ENOMEM;

	vpci_dev = modvm_device_alloc(ctx, "virtio-pci");
	if (!vpci_dev)
		return -ENOMEM;

	vpci_pdata.pci_bus = pci_bus;
	vpci_pdata.vdev = vdev_blk;
	vpci_pdata.devfn = PCI_AUTO_DEVFN;
	vpci_pdata.bar0_base = PCI_AUTO_MMIO;
	vpci_pdata.interrupt_pin = 1;
	vpci_pdata.mem_space = &ctx->accel.mem_space;

	ret = modvm_device_add(vpci_dev, &vpci_pdata);
	if (ret < 0) {
		modvm_device_put(vpci_dev);
		return ret;
	}

	return 0;
}

/**
 * modvm_hw_pc_init - assemble the legacy x86 personal computer topology
 * @ctx: the context instance being constructed
 *
 * Handles intelligent memory splitting around the PCI hole and wires up
 * standard legacy ISA/LPC components.
 *
 * Return: 0 upon successful assembly, or a negative error code.
 */
static int modvm_hw_pc_init(struct modvm_ctx *ctx)
{
	struct modvm_device *uart, *exit_dev, *pci_bridge;
	struct modvm_serial_pdata uart_pdata;
	struct pio_bridge_pdata bridge_pdata;
	struct pc_irq_route *route;
	struct modvm_pci_bus *pci_root_bus = NULL;
	uint64_t ram_size = ctx->config.ram_size;
	uint64_t low_ram;
	uint64_t high_ram = 0;
	int ret;
	int i;
	size_t d_idx;

	if (ram_size >= PC_LOW_RAM_MAX) {
		low_ram = PC_LOW_RAM_MAX;
		high_ram = ram_size - PC_LOW_RAM_MAX;
	} else {
		low_ram = ram_size;
	}

	ret = modvm_accel_map_ram(&ctx->accel, 0x00000000, low_ram,
				  MODVM_MEM_EXEC);
	if (ret < 0)
		return ret;

	if (high_ram > 0) {
		ret = modvm_accel_map_ram(&ctx->accel, PC_HIGH_RAM_BASE,
					  high_ram, MODVM_MEM_EXEC);
		if (ret < 0)
			return ret;
	}

	ret = modvm_accel_setup_irqchip(&ctx->accel);
	if (ret < 0)
		return ret;

	uart = modvm_device_alloc(ctx, "uart-16550a");
	if (!uart)
		return -ENOMEM;

	route = modvm_devm_zalloc(uart, sizeof(*route));
	if (!route) {
		modvm_device_put(uart);
		return -ENOMEM;
	}
	route->accel = &ctx->accel;
	route->gsi = 4;

	uart_pdata.bus_type = MODVM_BUS_PIO;
	uart_pdata.base = 0x3f8;
	uart_pdata.reg_shift = 0;
	uart_pdata.console = ctx->config.console;
	uart_pdata.event_loop = &ctx->event_loop;
	uart_pdata.irq =
		modvm_devm_irq_alloc(uart, modvm_hw_pc_irq_handler, route);
	if (!uart_pdata.irq) {
		modvm_device_put(uart);
		return -ENOMEM;
	}

	ret = modvm_device_add(uart, &uart_pdata);
	if (ret < 0) {
		modvm_device_put(uart);
		return ret;
	}

	pci_bridge = modvm_device_alloc(ctx, "pci-pio-bridge");
	if (!pci_bridge)
		return -ENOMEM;

	bridge_pdata.config_addr_port = 0xCF8;
	bridge_pdata.config_data_port = 0xCFC;
	bridge_pdata.mmio_base = PC_LOW_RAM_MAX;
	bridge_pdata.out_bus = &pci_root_bus;

	for (i = 0; i < 4; i++) {
		route = modvm_devm_zalloc(pci_bridge, sizeof(*route));
		if (!route) {
			modvm_device_put(pci_bridge);
			return -ENOMEM;
		}
		route->accel = &ctx->accel;
		route->gsi = 10 + i;
		bridge_pdata.pirq[i] = modvm_devm_irq_alloc(
			pci_bridge, modvm_hw_pc_irq_handler, route);
		if (!bridge_pdata.pirq[i]) {
			modvm_device_put(pci_bridge);
			return -ENOMEM;
		}
	}

	ret = modvm_device_add(pci_bridge, &bridge_pdata);
	if (ret < 0) {
		modvm_device_put(pci_bridge);
		return ret;
	}

	for (d_idx = 0; d_idx < ctx->config.nr_drives; d_idx++) {
		ret = modvm_hw_pc_add_virtio_blk(ctx, pci_root_bus,
						 ctx->config.drives[d_idx]);
		if (ret < 0) {
			pr_err("failed to mount virtio block device %zu\n",
			       d_idx);
			return ret;
		}
	}

	if (pci_root_bus)
		modvm_hw_pc_route_pci_irqs(pci_root_bus);

	exit_dev = modvm_device_alloc(ctx, "debug-exit");
	if (!exit_dev)
		return -ENOMEM;

	ret = modvm_device_add(exit_dev, NULL);
	if (ret < 0) {
		modvm_device_put(exit_dev);
		return ret;
	}

	return 0;
}

/**
 * modvm_hw_pc_reset - orchestrate the boot sequence for the PC architecture
 * @ctx: the context instance to reset
 * 
 * Return: 0 on success, or a negative error code.
 */
static int modvm_hw_pc_reset(struct modvm_ctx *ctx)
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
	.init = modvm_hw_pc_init,
	.reset = modvm_hw_pc_reset,
};

static const struct modvm_board pc_board = {
	.name = "pc",
	.desc = "Standard x86 Personal Computer",
	.ops = &pc_ops,
};

static void __attribute__((constructor)) modvm_hw_pc_register(void)
{
	modvm_board_register(&pc_board);
}