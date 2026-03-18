/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/bus.h>
#include <modvm/core/devm.h>
#include <modvm/core/modvm.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/log.h>
#include <modvm/utils/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "bus: " fmt

static void modvm_bus_region_release(struct modvm_bus_region *reg)
{
	list_del(&reg->node);
	pr_debug("automatically unregistered bus region at 0x%lx\n", reg->base);
}

/**
 * modvm_bus_register_region - map a device onto the system address space
 * @type: indicates whether this is PIO or MMIO
 * @base: absolute starting address on the system bus
 * @size: size of the claimed region in bytes
 * @dev: the peripheral device owning this region
 *
 * Scans the specific topology for overlapping regions to prevent hardware
 * resource collisions. The mapping is strictly tied to the device lifecycle.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_bus_register_region(enum modvm_bus_type type, uint64_t base,
			      uint64_t size, struct modvm_device *dev)
{
	struct modvm_ctx *ctx;
	struct modvm_bus_region *reg;
	struct modvm_bus_region *pos;
	struct list_head *list;
	int ret;

	if (WARN_ON(!dev || !dev->ctx || !dev->ops || size == 0))
		return -EINVAL;

	ctx = dev->ctx;
	list = (type == MODVM_BUS_PIO) ? &ctx->bus.pio_regions :
					 &ctx->bus.mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (base < pos->base + pos->size && base + size > pos->base) {
			pr_err("bus conflict detected at base 0x%lx\n", base);
			return -EBUSY;
		}
	}

	reg = modvm_devm_zalloc(dev, sizeof(*reg));
	if (!reg)
		return -ENOMEM;

	reg->dev = dev;
	reg->base = base;
	reg->size = size;
	reg->type = type;

	list_add_tail(&reg->node, list);

	ret = modvm_devm_add_action(dev, modvm_bus_region_release, reg);
	if (ret < 0) {
		list_del(&reg->node);
		return ret;
	}

	pr_debug("registered '%s' to space %d at 0x%lx\n",
		 dev->name ? dev->name : "unknown", type, base);
	return 0;
}

/**
 * modvm_bus_dispatch_read - route a read operation to the owning peripheral
 * @bus: the address space topology
 * @type: the target address space
 * @addr: the absolute requested address
 * @size: the size of the read request in bytes
 *
 * Return: the value supplied by the device, or ~0ULL if unmapped/out-of-bounds.
 */
uint64_t modvm_bus_dispatch_read(struct modvm_bus *bus,
				 enum modvm_bus_type type, uint64_t addr,
				 uint8_t size)
{
	struct modvm_bus_region *pos;
	struct list_head *list;
	uint64_t offset;

	if (WARN_ON(!bus))
		return ~0ULL;

	list = (type == MODVM_BUS_PIO) ? &bus->pio_regions : &bus->mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (addr >= pos->base && addr < pos->base + pos->size) {
			offset = addr - pos->base;

			if (unlikely(offset + size > pos->size)) {
				pr_warn("cross-boundary read intercepted at offset 0x%lx\n",
					offset);
				return ~0ULL;
			}

			if (likely(pos->dev->ops->read))
				return pos->dev->ops->read(pos->dev, offset,
							   size);

			return 0;
		}
	}

	/* Floating bus paradigm: unmapped electrical lines return high */
	return ~0ULL;
}

/**
 * modvm_bus_dispatch_write - route a write operation to the owning peripheral
 * @bus: the address space topology
 * @type: the target address space
 * @addr: the absolute requested address
 * @val: the payload to write
 * @size: the size of the write request in bytes
 */
void modvm_bus_dispatch_write(struct modvm_bus *bus, enum modvm_bus_type type,
			      uint64_t addr, uint64_t val, uint8_t size)
{
	struct modvm_bus_region *pos;
	struct list_head *list;
	uint64_t offset;

	if (WARN_ON(!bus))
		return;

	list = (type == MODVM_BUS_PIO) ? &bus->pio_regions : &bus->mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (addr >= pos->base && addr < pos->base + pos->size) {
			offset = addr - pos->base;

			if (unlikely(offset + size > pos->size)) {
				pr_warn("cross-boundary write intercepted at offset 0x%lx\n",
					offset);
				return;
			}

			if (likely(pos->dev->ops->write))
				pos->dev->ops->write(pos->dev, offset, val,
						     size);

			return;
		}
	}
}