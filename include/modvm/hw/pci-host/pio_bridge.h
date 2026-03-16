/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_PCI_HOST_PIO_BRIDGE_H
#define MODVM_HW_PCI_HOST_PIO_BRIDGE_H

#include <stdint.h>

struct modvm_irq;
struct modvm_pci_bus;

/**
 * struct pio_bridge_pdata - platform routing data for the PIO Bridge
 * @config_addr_port: PIO port for CONFIG_ADDRESS
 * @config_data_port: PIO port for CONFIG_DATA
 * @pirq: array mapping the 4 standard PCI routing lines (PIRQA-PIRQD) to system GSIs
 * @out_bus: OUT parameter; bridge will populate this with its logical bus pointer
 */
struct pio_bridge_pdata {
	uint16_t config_addr_port;
	uint16_t config_data_port;
	struct modvm_irq *pirq[4];
	struct modvm_pci_bus **out_bus;
};

#endif /* MODVM_HW_PCI_HOST_PIO_BRIDGE_H */