/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_BUS_H
#define MODVM_CORE_BUS_H

#include <stdint.h>
#include <modvm/core/device.h>
#include <modvm/utils/list.h>

struct modvm_ctx;

/**
 * enum modvm_bus_type - memory and port address spaces
 * @MODVM_BUS_PIO: port I/O space, relied upon by legacy x86 architectures
 * @MODVM_BUS_MMIO: memory-mapped I/O space used universally
 */
enum modvm_bus_type {
	MODVM_BUS_PIO,
	MODVM_BUS_MMIO,
};

/**
 * struct modvm_bus_region - claimed address range on the system bus
 * @node: linked list node for routing iterations
 * @dev: the peripheral device owning this region
 * @base: absolute starting address on the system bus
 * @size: size of the claimed region in bytes
 * @type: indicates whether this is port I/O or memory-mapped I/O
 */
struct modvm_bus_region {
	struct list_head node;
	struct modvm_device *dev;
	uint64_t base;
	uint64_t size;
	enum modvm_bus_type type;
};

int modvm_bus_register_region(enum modvm_bus_type type, uint64_t base,
			      uint64_t size, struct modvm_device *dev);
uint64_t modvm_bus_dispatch_read(struct modvm_ctx *ctx,
				 enum modvm_bus_type type, uint64_t addr,
				 uint8_t size);
void modvm_bus_dispatch_write(struct modvm_ctx *ctx, enum modvm_bus_type type,
			      uint64_t addr, uint64_t val, uint8_t size);

#endif /* MODVM_CORE_BUS_H */