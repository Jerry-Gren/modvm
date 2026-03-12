/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_IRQ_H
#define MODVM_CORE_IRQ_H

#include <modvm/core/device.h>

struct modvm_irq;

/**
 * typedef modvm_irq_cb_t - callback invoked upon interrupt state change
 * @data: contextual data pointer provided during allocation
 * @level: the logical voltage level representing the line state (0 or 1)
 */
typedef void (*modvm_irq_cb_t)(void *data, int level);

struct modvm_irq *modvm_devm_irq_alloc(struct modvm_device *dev,
				       modvm_irq_cb_t cb, void *data);
void modvm_irq_set_level(struct modvm_irq *irq, int level);

#endif /* MODVM_CORE_IRQ_H */