/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef MODVM_CORE_PCI_H
#define MODVM_CORE_PCI_H

#include <stdint.h>
#include <modvm/core/device.h>
#include <modvm/utils/list.h>

#define PCI_CONFIG_SPACE_SIZE 256

/* Standard PCI Configuration Space Offsets */
#define PCI_INTERRUPT_LINE 0x3C
#define PCI_INTERRUPT_PIN 0x3D

#define PCI_AUTO_DEVFN 0xFF
#define PCI_AUTO_MMIO 0ULL

struct modvm_pci_device;
struct modvm_pci_bus;

/**
 * typedef modvm_pci_set_irq_cb_t - callback for host bridge interrupt routing
 * @data: closure payload provided by the host bridge
 * @pci_dev: the endpoint device asserting or deasserting the interrupt
 * @level: logical voltage level
 */
typedef void (*modvm_pci_set_irq_cb_t)(void *data,
				       struct modvm_pci_device *pci_dev,
				       int level);

/**
 * struct modvm_pci_device_ops - operations for specific PCI endpoints
 * @read_config: handle reads to the 256-byte PCI configuration space
 * @write_config: handle writes to the PCI configuration space
 */
struct modvm_pci_device_ops {
	uint32_t (*read_config)(struct modvm_pci_device *pci_dev,
				uint8_t offset, uint8_t size);
	void (*write_config)(struct modvm_pci_device *pci_dev, uint8_t offset,
			     uint32_t val, uint8_t size);
};

/**
 * struct modvm_pci_device - abstract base class for PCI endpoint devices
 * @node: linked list node for the host bridge's device registry
 * @bus: pointer to the parent PCI bus segment
 * @parent_dev: the underlying generic modvm_device
 * @ops: dispatch table for PCI-specific operations
 * @priv: opaque pointer for endpoint-specific state
 * @devfn: Device and Function number
 * @interrupt_pin: PCI interrupt pin (0=None, 1=INTA, 2=INTB, 3=INTC, 4=INTD)
 * @interrupt_line: mapped system IRQ number (for guest OS discovery)
 * @config_space: cached 256-byte PCI configuration space layout
 */
struct modvm_pci_device {
	struct list_head node;
	struct modvm_pci_bus *bus;
	struct modvm_device *parent_dev;
	const struct modvm_pci_device_ops *ops;
	void *priv;

	uint8_t devfn;
	uint8_t interrupt_pin;
	uint8_t interrupt_line;
	uint8_t config_space[PCI_CONFIG_SPACE_SIZE];
};

/**
 * struct modvm_pci_bus - abstract representation of a PCI bus segment
 * @devices: list of PCI endpoint devices attached to this bus
 * @set_irq_cb: host bridge hook for intercepting and swizzling interrupts
 * @set_irq_data: host bridge closure data
 */
struct modvm_pci_bus {
	struct list_head devices;
	modvm_pci_set_irq_cb_t set_irq_cb;
	void *set_irq_data;

	uint8_t next_devfn;
	uint64_t mmio_alloc_cursor;
};

void modvm_pci_bus_init(struct modvm_pci_bus *bus, uint64_t mmio_base,
			modvm_pci_set_irq_cb_t set_irq_cb, void *set_irq_data);
uint64_t modvm_pci_bus_alloc_mmio(struct modvm_pci_bus *bus, size_t size);
int modvm_pci_device_register(struct modvm_pci_bus *bus,
			      struct modvm_pci_device *pci_dev);
uint32_t modvm_pci_bus_read_config(struct modvm_pci_bus *bus, uint8_t devfn,
				   uint8_t offset, uint8_t size);
void modvm_pci_bus_write_config(struct modvm_pci_bus *bus, uint8_t devfn,
				uint8_t offset, uint32_t val, uint8_t size);
void modvm_pci_device_set_irq(struct modvm_pci_device *pci_dev, int level);

#endif /* MODVM_CORE_PCI_H */