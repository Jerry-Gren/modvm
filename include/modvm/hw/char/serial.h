/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_CHAR_SERIAL_H
#define MODVM_HW_CHAR_SERIAL_H

#include <stdint.h>
#include <modvm/core/irq.h>
#include <modvm/core/chardev.h>

struct modvm_event_loop;

/**
 * struct modvm_serial_pdata - hardwired configuration for serial devices
 * @base: the starting address on the I/O bus
 * @irq: the pre-wired interrupt line to signal the processor
 * @console: the host character device backend for data stream routing
 * @event_loop: the event dispatcher for asynchronous rx notification
 */
struct modvm_serial_pdata {
	uint64_t base;
	struct modvm_irq *irq;
	struct modvm_chardev *console;
	struct modvm_event_loop *event_loop;
};

#endif /* MODVM_HW_CHAR_SERIAL_H */