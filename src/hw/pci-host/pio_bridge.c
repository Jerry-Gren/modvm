/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/device.h>
#include <modvm/core/bus.h>
#include <modvm/core/devm.h>
#include <modvm/core/pci.h>
#include <modvm/core/irq.h>
#include <modvm/hw/pci-host/pio_bridge.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/log.h>
#include <modvm/utils/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "pio_bridge: " fmt

/**
 * struct pio_bridge_ctx - state container for the x86 PIO PCI Host Bridge
 * @config_addr: latched address for the subsequent data port access
 * @bus: the core PCI bus segment managed by this bridge
 * @pirq: array mapping the 4 standard PCI routing lines (PIRQA-PIRQD) to system GSIs
 */
struct pio_bridge_ctx {
	uint32_t config_addr;
	struct modvm_pci_bus bus;
	struct modvm_irq *pirq[4];
};

static void pio_bridge_set_irq_cb(void *data, struct modvm_pci_device *pci_dev,
				  int level)
{
	struct pio_bridge_ctx *ctx = data;
	uint8_t slot;
	uint8_t pin;
	uint8_t pirq_idx;

	if (unlikely(!pci_dev))
		return;

	pin = pci_dev->interrupt_pin;
	if (unlikely(pin == 0 || pin > 4))
		return;

	slot = pci_dev->devfn >> 3;
	pirq_idx = (slot + pin - 1) % 4;

	if (likely(ctx->pirq[pirq_idx]))
		modvm_irq_set_level(ctx->pirq[pirq_idx], level);
}

static uint64_t pio_bridge_read(struct modvm_device *dev, uint64_t offset,
				uint8_t size)
{
	struct pio_bridge_ctx *ctx = dev->priv;
	uint8_t bus_num, devfn, reg;

	if (offset == 0) {
		if (likely(size == 4))
			return ctx->config_addr;
		return ~0ULL;
	}

	if (likely(offset >= 4)) {
		if (unlikely(!(ctx->config_addr & 0x80000000)))
			return ~0ULL;

		bus_num = (ctx->config_addr >> 16) & 0xFF;
		devfn = (ctx->config_addr >> 8) & 0xFF;
		reg = (ctx->config_addr & 0xFC) + (offset - 4);

		if (unlikely(bus_num != 0))
			return ~0ULL;

		return modvm_pci_bus_read_config(&ctx->bus, devfn, reg, size);
	}

	return ~0ULL;
}

static void pio_bridge_write(struct modvm_device *dev, uint64_t offset,
			     uint64_t val, uint8_t size)
{
	struct pio_bridge_ctx *ctx = dev->priv;
	uint8_t bus_num, devfn, reg;

	if (offset == 0) {
		if (likely(size == 4))
			ctx->config_addr = (uint32_t)val;
		return;
	}

	if (likely(offset >= 4)) {
		if (unlikely(!(ctx->config_addr & 0x80000000)))
			return;

		bus_num = (ctx->config_addr >> 16) & 0xFF;
		devfn = (ctx->config_addr >> 8) & 0xFF;
		reg = (ctx->config_addr & 0xFC) + (offset - 4);

		if (unlikely(bus_num != 0))
			return;

		modvm_pci_bus_write_config(&ctx->bus, devfn, reg, (uint32_t)val,
					   size);
	}
}

static const struct modvm_device_ops pio_bridge_ops = {
	.read = pio_bridge_read,
	.write = pio_bridge_write,
};

static int pio_bridge_instantiate(struct modvm_device *dev, void *pdata)
{
	struct pio_bridge_pdata *plat = pdata;
	struct pio_bridge_ctx *ctx;
	int ret;
	int i;

	if (WARN_ON(!plat))
		return -EINVAL;

	ctx = modvm_devm_zalloc(dev, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	/* Bind the interrupt routing closure to the abstract bus */
	modvm_pci_bus_init(&ctx->bus, plat->mmio_base, pio_bridge_set_irq_cb,
			   ctx);
	ctx->config_addr = 0;

	for (i = 0; i < 4; i++)
		ctx->pirq[i] = plat->pirq[i];

	dev->ops = &pio_bridge_ops;
	dev->priv = ctx;

	ret = modvm_bus_register_region(MODVM_BUS_PIO, plat->config_addr_port,
					8, dev);
	if (ret < 0)
		return ret;

	if (plat->out_bus)
		*plat->out_bus = &ctx->bus;

	pr_info("pio pci host bridge online at ports 0x%x/0x%x\n",
		plat->config_addr_port, plat->config_data_port);
	return 0;
}

static const struct modvm_device_class pio_bridge_class = {
	.name = "pci-pio-bridge",
	.instantiate = pio_bridge_instantiate,
};

static void __attribute__((constructor)) register_pio_bridge_class(void)
{
	modvm_device_class_register(&pio_bridge_class);
}