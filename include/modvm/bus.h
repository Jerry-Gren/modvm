/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_BUS_H
#define MODVM_BUS_H

#include <modvm/device.h>
#include <modvm/list.h>

/*
 * Address space definitions.
 * PIO is heavily relied upon by legacy x86 hardware.
 * MMIO is the modern standard used by ARM64, RISC-V, and modern x86.
 */
enum vm_bus_space {
	VM_BUS_SPACE_PIO,
	VM_BUS_SPACE_MMIO,
};

/**
 * struct vm_bus_region - Represents a claimed address range on the system bus
 * @node: Linked list head for routing iterations
 * @dev: The device owning this region
 * @base_addr: Absolute starting address on the system bus
 * @length: Size of the claimed region in bytes
 * @space_type: Indicates whether this is PIO or MMIO
 */
struct vm_bus_region {
	struct list_head node;
	struct vm_device *dev;
	uint64_t base_addr;
	uint64_t length;
	enum vm_bus_space space_type;
};

int bus_register_region(enum vm_bus_space space, uint64_t base, uint64_t length,
			struct vm_device *dev);

uint64_t bus_dispatch_read(enum vm_bus_space space, uint64_t addr,
			   uint8_t size);
void bus_dispatch_write(enum vm_bus_space space, uint64_t addr, uint64_t value,
			uint8_t size);

#endif