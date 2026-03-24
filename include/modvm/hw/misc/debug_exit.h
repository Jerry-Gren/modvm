/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_MISC_DEBUG_EXIT_H
#define MODVM_HW_MISC_DEBUG_EXIT_H

#include <stdint.h>
#include <modvm/core/bus.h>

/**
 * struct modvm_debug_exit_pdata - platform routing data for debug exit device
 * @bus_type: the target address space (PIO or MMIO)
 * @base: the absolute starting address on the system bus
 */
struct modvm_debug_exit_pdata {
	enum modvm_bus_type bus_type;
	uint64_t base;
};

#endif /* MODVM_HW_MISC_DEBUG_EXIT_H */