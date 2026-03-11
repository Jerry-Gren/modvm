/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_IRQ_H
#define MODVM_CORE_IRQ_H

#include <modvm/core/device.h>

struct vm_irq;

/**
 * typedef vm_irq_cb_t - callback invoked upon interrupt state change.
 * @data: contextual data pointer provided during allocation.
 * @level: the logical voltage level representing the line state (0 or 1).
 */
typedef void (*vm_irq_cb_t)(void *data, int level);

struct vm_irq *vm_devm_irq_alloc(struct vm_device *dev, vm_irq_cb_t cb,
				 void *data);

void vm_irq_set_level(struct vm_irq *irq, int level);

#endif /* MODVM_CORE_IRQ_H */