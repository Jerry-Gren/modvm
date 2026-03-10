/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_SERIAL_H
#define MODVM_HW_SERIAL_H

#include <stdint.h>
#include <modvm/core/interrupt_line.h>
#include <modvm/core/character_device.h>

/**
 * struct vm_serial_platform_data - hardwired configuration for serial devices
 * @base_port_address: the starting address on the I/O bus
 * @interrupt_line: the pre-wired interrupt line to signal the processor
 * @console_backend: the host character device backend for data stream routing
 *
 * Provides immutable hardware routing information to the serial device class
 * during the device instantiation phase.
 */
struct vm_serial_platform_data {
	uint64_t base_port_address;
	struct vm_interrupt_line *interrupt_line;
	struct vm_character_device *console_backend;
};

#endif /* MODVM_HW_SERIAL_H */