/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_SERIAL_H
#define MODVM_HW_SERIAL_H

#include <stdint.h>
#include <modvm/irq.h>
#include <modvm/chardev.h>

/**
 * struct serial_platform_data - hardwired configuration for serial devices
 * @base_port: the starting address on the I/O bus
 * @irq: the pre-wired interrupt line to signal the processor
 * @chr: the host character device backend for data stream routing
 */
struct serial_platform_data {
	uint64_t base_port;
	struct vm_irq *irq;
	struct vm_chardev *chr;
};

#endif /* MODVM_HW_SERIAL_H */