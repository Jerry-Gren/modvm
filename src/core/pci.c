/* SPDX-License-Identifier: GPL-2.0 */
#include <errno.h>

#include <modvm/core/pci.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/log.h>
#include <modvm/utils/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "pci: " fmt

/**
 * modvm_pci_bus_init - initialize a virtual PCI bus topology
 * @bus: the PCI bus instance to initialize
 * @mmio_base: starting physical address for dynamic MMIO window allocations
 * @set_irq_cb: host bridge hook for interrupt routing
 * @sys_data: host bridge closure data
 */
void modvm_pci_bus_init(struct modvm_pci_bus *bus, uint64_t mmio_base,
			modvm_pci_set_irq_cb_t set_irq_cb, void *sys_data)
{
	if (WARN_ON(!bus))
		return;

	INIT_LIST_HEAD(&bus->devices);
	bus->set_irq_cb = set_irq_cb;
	bus->sys_data = sys_data;

	bus->next_devfn = 8;
	bus->mmio_alloc_cursor = mmio_base;
}

/**
 * modvm_pci_bus_alloc_mmio - allocate a contiguous block of PCI MMIO space
 * @bus: the PCI bus managing the memory window
 * @size: requested size in bytes (must be a power of 2)
 *
 * Return: absolute physical base address, or 0 on failure.
 */
uint64_t modvm_pci_bus_alloc_mmio(struct modvm_pci_bus *bus, size_t size)
{
	uint64_t base;

	if (WARN_ON(!bus || size == 0))
		return 0;

	base = (bus->mmio_alloc_cursor + size - 1) & ~(size - 1);
	bus->mmio_alloc_cursor = base + size;

	return base;
}

/**
 * modvm_pci_device_register - attach a PCI endpoint to the abstract bus
 * @bus: the target PCI bus
 * @pci_dev: the endpoint device to attach
 *
 * Populates immutable configuration space fields and handles dynamic devfn
 * allocation if requested. System IRQ routing is deferred to the firmware.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_pci_device_register(struct modvm_pci_bus *bus,
			      struct modvm_pci_device *pci_dev)
{
	struct modvm_pci_device *pos;

	if (WARN_ON(!bus || !pci_dev))
		return -EINVAL;

	if (pci_dev->devfn == PCI_AUTO_DEVFN) {
		if (WARN_ON(bus->next_devfn > 255)) {
			pr_err("PCI bus exhausted, no available devfn\n");
			return -ENOSPC;
		}
		pci_dev->devfn = bus->next_devfn;
		bus->next_devfn += 8;
	}

	list_for_each_entry(pos, &bus->devices, node)
	{
		if (pos->devfn == pci_dev->devfn) {
			pr_err("PCI slot collision detected at devfn %u\n",
			       pci_dev->devfn);
			return -EBUSY;
		}
	}

	pci_dev->bus = bus;

	pci_dev->config_space[PCI_INTERRUPT_PIN] = pci_dev->interrupt_pin;
	pci_dev->config_space[PCI_INTERRUPT_LINE] = 0;

	list_add_tail(&pci_dev->node, &bus->devices);
	pr_info("registered PCI device at devfn %u (pin %u)\n", pci_dev->devfn,
		pci_dev->interrupt_pin);

	return 0;
}

static struct modvm_pci_device *
modvm_pci_bus_find_device(struct modvm_pci_bus *bus, uint8_t devfn)
{
	struct modvm_pci_device *pos;

	list_for_each_entry(pos, &bus->devices, node)
	{
		if (pos->devfn == devfn)
			return pos;
	}

	return NULL;
}

/**
 * modvm_pci_bus_read_config - route a configuration space read to an endpoint
 * @bus: the PCI bus containing the target device
 * @devfn: the target device and function number
 * @offset: byte offset within the 256-byte configuration space
 * @size: size of the read request
 *
 * Return: the requested register value, or ~0U if unmapped.
 */
uint32_t modvm_pci_bus_read_config(struct modvm_pci_bus *bus, uint8_t devfn,
				   uint8_t offset, uint8_t size)
{
	struct modvm_pci_device *pci_dev;

	if (unlikely(offset + size > PCI_CONFIG_SPACE_SIZE))
		return ~0U;

	pci_dev = modvm_pci_bus_find_device(bus, devfn);
	if (unlikely(!pci_dev))
		return ~0U;

	if (likely(pci_dev->ops && pci_dev->ops->read_config))
		return pci_dev->ops->read_config(pci_dev, offset, size);

	return ~0U;
}

/**
 * modvm_pci_bus_write_config - route a configuration space write to an endpoint
 * @bus: the PCI bus containing the target device
 * @devfn: the target device and function number
 * @offset: byte offset within the 256-byte configuration space
 * @val: the payload to write
 * @size: size of the write request
 */
void modvm_pci_bus_write_config(struct modvm_pci_bus *bus, uint8_t devfn,
				uint8_t offset, uint32_t val, uint8_t size)
{
	struct modvm_pci_device *pci_dev;

	if (unlikely(offset + size > PCI_CONFIG_SPACE_SIZE))
		return;

	pci_dev = modvm_pci_bus_find_device(bus, devfn);
	if (unlikely(!pci_dev))
		return;

	if (likely(pci_dev->ops && pci_dev->ops->write_config))
		pci_dev->ops->write_config(pci_dev, offset, val, size);
}

/**
 * modvm_pci_device_set_irq - delegate interrupt assertion to the parent bus
 * @pci_dev: the endpoint device triggering the interrupt
 * @level: logical voltage level
 *
 * This function belongs to the event delivery hot path.
 */
void modvm_pci_device_set_irq(struct modvm_pci_device *pci_dev, int level)
{
	struct modvm_pci_bus *bus;

	if (unlikely(!pci_dev || !pci_dev->bus))
		return;

	bus = pci_dev->bus;

	if (likely(bus->set_irq_cb))
		bus->set_irq_cb(bus->sys_data, pci_dev, level);
}