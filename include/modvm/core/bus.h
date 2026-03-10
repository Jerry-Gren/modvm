/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_BUS_H
#define MODVM_CORE_BUS_H

#include <stdint.h>
#include <modvm/core/device.h>
#include <modvm/utils/list.h>

/**
 * enum vm_bus_space_type - memory and port address spaces
 * @VM_BUS_SPACE_PORT_IO: port I/O space, heavily relied upon by legacy x86
 * @VM_BUS_SPACE_MEMORY_MAPPED_IO: memory-mapped I/O space used by modern architectures
 */
enum vm_bus_space_type {
	VM_BUS_SPACE_PORT_IO,
	VM_BUS_SPACE_MEMORY_MAPPED_IO,
};

/**
 * struct vm_bus_region - claimed address range on the system bus
 * @node: linked list head for routing iterations
 * @device: the peripheral device owning this region
 * @base_address: absolute starting address on the system bus
 * @length_bytes: size of the claimed region in bytes
 * @space_type: indicates whether this is port I/O or memory-mapped I/O
 */
struct vm_bus_region {
	struct list_head node;
	struct vm_device *device;
	uint64_t base_address;
	uint64_t length_bytes;
	enum vm_bus_space_type space_type;
};

int vm_bus_register_region(enum vm_bus_space_type space, uint64_t base_address,
			   uint64_t length_bytes, struct vm_device *device);

uint64_t vm_bus_dispatch_read(enum vm_bus_space_type space, uint64_t address,
			      uint8_t access_size);

void vm_bus_dispatch_write(enum vm_bus_space_type space, uint64_t address,
			   uint64_t value, uint8_t access_size);

#endif /* MODVM_CORE_BUS_H */