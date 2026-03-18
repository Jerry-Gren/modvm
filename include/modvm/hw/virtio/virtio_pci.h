/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_VIRTIO_VIRTIO_PCI_H
#define MODVM_HW_VIRTIO_VIRTIO_PCI_H

#include <stdint.h>

struct modvm_pci_bus;
struct virtio_device;
struct modvm_mem_space;

/**
 * struct virtio_pci_pdata - platform routing data for a Virtio-PCI transport
 * @pci_bus: the PCI host bridge bus to attach to
 * @vdev: the specific virtio backend payload to wrap
 * @devfn: requested PCI Device and Function number (e.g., Slot << 3)
 * @interrupt_pin: PCI interrupt pin (1=INTA, 2=INTB, 3=INTC, 4=INTD)
 * @bar0_base: MMIO base address allocated by the motherboard for BAR0
 * @mem_space: injected physical memory space
 */
struct virtio_pci_pdata {
	struct modvm_pci_bus *pci_bus;
	struct virtio_device *vdev;
	uint8_t devfn;
	uint8_t interrupt_pin;
	uint64_t bar0_base;
	struct modvm_mem_space *mem_space;
};

#endif /* MODVM_HW_VIRTIO_VIRTIO_PCI_H */