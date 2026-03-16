/* SPDX-License-Identifier: GPL-2.0 */
#include <errno.h>

#include <modvm/core/pci.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/log.h>
#include <modvm/utils/compiler.h>

/**
 * modvm_pci_bus_init - initialize a virtual PCI bus topology
 * @bus: the PCI bus instance to initialize
 * @set_irq_cb: host bridge hook for interrupt routing
 * @set_irq_data: host bridge closure data
 */
void modvm_pci_bus_init(struct modvm_pci_bus *bus,
			modvm_pci_set_irq_cb_t set_irq_cb, void *set_irq_data)
{
	if (WARN_ON(!bus))
		return;

	INIT_LIST_HEAD(&bus->devices);
	bus->set_irq_cb = set_irq_cb;
	bus->set_irq_data = set_irq_data;
}

/**
 * modvm_pci_device_register - attach a PCI endpoint to the abstract bus
 * @bus: the target PCI bus
 * @pci_dev: the endpoint device to attach
 *
 * Populates immutable configuration space fields and mounts the device.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_pci_device_register(struct modvm_pci_bus *bus,
			      struct modvm_pci_device *pci_dev)
{
	struct modvm_pci_device *pos;

	if (WARN_ON(!bus || !pci_dev))
		return -EINVAL;

	list_for_each_entry(pos, &bus->devices, node)
	{
		if (pos->devfn == pci_dev->devfn) {
			pr_err("PCI slot collision detected at devfn %u\n",
			       pci_dev->devfn);
			return -EBUSY;
		}
	}

	pci_dev->bus = bus;

	/* Architecturally expose the hardware routing to the guest OS */
	pci_dev->config_space[PCI_INTERRUPT_PIN] = pci_dev->interrupt_pin;
	pci_dev->config_space[PCI_INTERRUPT_LINE] = pci_dev->interrupt_line;

	list_add_tail(&pci_dev->node, &bus->devices);
	pr_info("registered PCI device at devfn %u (pin %u, irq %u)\n",
		pci_dev->devfn, pci_dev->interrupt_pin,
		pci_dev->interrupt_line);

	return 0;
}

static struct modvm_pci_device *pci_bus_find_device(struct modvm_pci_bus *bus,
						    uint8_t devfn)
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

	pci_dev = pci_bus_find_device(bus, devfn);
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

	pci_dev = pci_bus_find_device(bus, devfn);
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
		bus->set_irq_cb(bus->set_irq_data, pci_dev, level);
}