/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_CHAR_SERIAL_H
#define MODVM_HW_CHAR_SERIAL_H

#include <stdint.h>
#include <modvm/core/irq.h>
#include <modvm/core/bus.h>
#include <modvm/core/chardev.h>

struct modvm_event_loop;

/**
 * struct modvm_serial_pdata - hardware configuration for serial devices
 * @bus_type: target system address space (PIO or MMIO)
 * @base: the starting address on the target bus
 * @reg_shift: byte shift for register spacing (0 for 8-bit, 2 for 32-bit aligned)
 * @irq: the pre-wired interrupt line to signal the processor
 * @console: the host character device backend for data stream routing
 * @event_loop: the event dispatcher for asynchronous rx notification
 */
struct modvm_serial_pdata {
	enum modvm_bus_type bus_type;
	uint64_t base;
	uint8_t reg_shift;
	struct modvm_irq *irq;
	struct modvm_chardev *console;
	struct modvm_event_loop *event_loop;
};

#endif /* MODVM_HW_CHAR_SERIAL_H */