/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_SERIAL_H
#define MODVM_HW_SERIAL_H

#include <stdint.h>
#include <modvm/core/irq.h>
#include <modvm/core/chardev.h>

/**
 * struct serial_pdata - hardwired configuration for serial devices.
 * @base: the starting address on the I/O bus.
 * @irq: the pre-wired interrupt line to signal the processor.
 * @console: the host character device backend for data stream routing.
 *
 * Provides immutable hardware routing information (platform data) to the 
 * serial device class during the device instantiation phase.
 */
struct serial_pdata {
	uint64_t base;
	struct vm_irq *irq;
	struct vm_chardev *console;
};

#endif /* MODVM_HW_SERIAL_H */