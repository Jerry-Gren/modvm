/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/irq.h>
#include <modvm/core/devm.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

struct modvm_irq {
	modvm_irq_cb_t cb;
	void *data;
};

/**
 * modvm_devm_irq_alloc - allocate a device-managed interrupt line
 * @dev: the device to manage this interrupt's lifecycle
 * @cb: the function to invoke upon state change
 * @data: contextual closure payload
 *
 * The allocated IRQ line will be automatically freed when the device
 * is destroyed via modvm_device_put.
 *
 * Return: allocated interrupt line, or NULL on failure.
 */
struct modvm_irq *modvm_devm_irq_alloc(struct modvm_device *dev,
				       modvm_irq_cb_t cb, void *data)
{
	struct modvm_irq *irq;

	if (WARN_ON(!dev || !cb))
		return NULL;

	irq = modvm_devm_zalloc(dev, sizeof(*irq));
	if (!irq)
		return NULL;

	irq->cb = cb;
	irq->data = data;

	return irq;
}

/**
 * modvm_irq_set_level - assert or deassert the virtual interrupt line
 * @irq: the interrupt line instance
 * @level: the logical voltage level (1 for high, 0 for low)
 */
void modvm_irq_set_level(struct modvm_irq *irq, int level)
{
	if (likely(irq && irq->cb))
		irq->cb(irq->data, level);
}