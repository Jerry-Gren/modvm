/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_BOARD_H
#define MODVM_CORE_BOARD_H

#include <modvm/core/modvm.h>

/**
 * struct modvm_board_ops - routines for wiring up a specific motherboard topology.
 * @init: early initialization (e.g., RAM mapping, pre-vCPU IRQ controllers).
 * @late_init: post-vCPU initialization (e.g., ARM64 VGIC redistributors).
 * @reset: configure initial CPU states and load firmware.
 */
struct modvm_board_ops {
	int (*init)(struct modvm_ctx *ctx);
	int (*late_init)(struct modvm_ctx *ctx);
	int (*reset)(struct modvm_ctx *ctx);
};

/**
 * struct modvm_board - static blueprint for a motherboard.
 * @name: identifier used in command line arguments (e.g., "pc", "virt").
 * @desc: human-readable description.
 * @ops: pointer to the operational methods.
 */
struct modvm_board {
	const char *name;
	const char *desc;
	const struct modvm_board_ops *ops;
};

void modvm_board_register(const struct modvm_board *board);
const struct modvm_board *modvm_board_find(const char *name);

#endif /* MODVM_CORE_BOARD_H */