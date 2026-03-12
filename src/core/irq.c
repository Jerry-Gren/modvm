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

static void irq_release_action(struct modvm_irq *irq)
{
	free(irq);
}

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
	int ret;

	if (WARN_ON(!dev || !cb))
		return NULL;

	irq = calloc(1, sizeof(*irq));
	if (unlikely(!irq))
		return NULL;

	irq->cb = cb;
	irq->data = data;

	/* Leverages the type-safe devm_add_action macro */
	ret = modvm_devm_add_action(dev, irq_release_action, irq);
	if (unlikely(ret < 0)) {
		free(irq);
		return NULL;
	}

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