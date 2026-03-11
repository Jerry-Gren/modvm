/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/bus.h>
#include <modvm/core/devres.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/log.h>

#undef pr_fmt
#define pr_fmt(fmt) "bus: " fmt

/**
 * bus_region_release - automatically unregister a bus region upon device destruction.
 * @owner: the peripheral device owning the region.
 * @res: the resource payload containing the bus region.
 *
 * This callback is invoked by the devres framework. It safely detaches
 * the region from the global bus topology before the memory is freed.
 */
static void bus_region_release(void *owner, void *res)
{
	struct vm_bus_region *reg = res;

	(void)owner;
	list_del(&reg->node);
	pr_debug("automatically unregistered region at 0x%lx\n", reg->base);
}

/**
 * vm_bus_register_region - map a device into the system address space.
 * @type: indicates whether this is PIO or MMIO.
 * @base: absolute starting address on the system bus.
 * @size: size of the claimed region in bytes.
 * @dev: the peripheral device owning this region.
 *
 * Scans the existing topology for overlapping regions to prevent
 * hardware resource collisions before registering the new mapping.
 * The mapping is tied to the device's lifecycle via devres.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_bus_register_region(enum vm_bus_type type, uint64_t base, uint64_t size,
			   struct vm_device *dev)
{
	struct vm_machine *machine;
	struct vm_bus_region *reg;
	struct vm_bus_region *pos;
	struct list_head *list;

	if (WARN_ON(!dev || !dev->machine || !dev->ops || size == 0))
		return -EINVAL;

	machine = dev->machine;
	list = (type == VM_BUS_PIO) ? &machine->bus.pio_regions :
				      &machine->bus.mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (base < pos->base + pos->size && base + size > pos->base) {
			pr_err("bus conflict detected at base 0x%lx\n", base);
			return -EBUSY;
		}
	}

	reg = vm_devres_alloc(bus_region_release, sizeof(*reg));
	if (!reg)
		return -ENOMEM;

	reg->dev = dev;
	reg->base = base;
	reg->size = size;
	reg->type = type;

	list_add_tail(&reg->node, list);
	vm_devres_add(dev, reg);

	pr_debug("registered '%s' to space %d at 0x%lx\n",
		 dev->name ? dev->name : "unknown", type, base);

	return 0;
}

uint64_t vm_bus_dispatch_read(struct vm_machine *machine, enum vm_bus_type type,
			      uint64_t addr, uint8_t size)
{
	struct vm_bus_region *pos;
	struct list_head *list;
	uint64_t offset;

	if (WARN_ON(!machine))
		return ~0ULL;

	list = (type == VM_BUS_PIO) ? &machine->bus.pio_regions :
				      &machine->bus.mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (addr >= pos->base && addr < pos->base + pos->size) {
			offset = addr - pos->base;

			/* Strict boundary enforcement to prevent hypervisor out-of-bounds access */
			if (offset + size > pos->size) {
				pr_warn("cross-boundary read intercepted at offset 0x%lx\n",
					offset);
				return ~0ULL;
			}

			if (pos->dev->ops->read)
				return pos->dev->ops->read(pos->dev, offset,
							   size);

			return 0;
		}
	}

	/* Simulate floating bus behavior where unmapped lines return high */
	return ~0ULL;
}

void vm_bus_dispatch_write(struct vm_machine *machine, enum vm_bus_type type,
			   uint64_t addr, uint64_t val, uint8_t size)
{
	struct vm_bus_region *pos;
	struct list_head *list;
	uint64_t offset;

	if (WARN_ON(!machine))
		return;

	list = (type == VM_BUS_PIO) ? &machine->bus.pio_regions :
				      &machine->bus.mmio_regions;

	list_for_each_entry(pos, list, node)
	{
		if (addr >= pos->base && addr < pos->base + pos->size) {
			offset = addr - pos->base;

			/* Strict boundary enforcement */
			if (offset + size > pos->size) {
				pr_warn("cross-boundary write intercepted at offset 0x%lx\n",
					offset);
				return;
			}

			if (pos->dev->ops->write)
				pos->dev->ops->write(pos->dev, offset, val,
						     size);

			return;
		}
	}
}