/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>

#include <modvm/core/irq.h>
#include <modvm/core/devres.h>

struct vm_irq {
	vm_irq_cb_t cb;
	void *data;
};

/**
 * vm_irq_alloc - allocate a new virtual interrupt line.
 * @cb: the function to execute when the line's voltage level changes.
 * @data: opaque pointer passed to the callback function.
 *
 * return: a pointer to the newly allocated interrupt line, or NULL on failure.
 */
static struct vm_irq *vm_irq_alloc(vm_irq_cb_t cb, void *data)
{
	struct vm_irq *irq;

	irq = calloc(1, sizeof(*irq));
	if (!irq)
		return NULL;

	irq->cb = cb;
	irq->data = data;

	return irq;
}

/**
 * vm_irq_free - destroy an interrupt line and release its resources.
 * @irq: the interrupt line to free.
 */
static void vm_irq_free(struct vm_irq *irq)
{
	free(irq);
}

/**
 * vm_devm_irq_alloc - allocate a device-managed interrupt line.
 * @dev: the device to manage this interrupt's lifecycle.
 * @cb: the handler function.
 * @data: contextual payload.
 *
 * return: allocated interrupt line, or NULL on failure.
 */
struct vm_irq *vm_devm_irq_alloc(struct vm_device *dev, vm_irq_cb_t cb,
				 void *data)
{
	struct vm_irq *irq;

	irq = vm_irq_alloc(cb, data);
	if (!irq)
		return NULL;

	if (vm_devm_add_action(dev, (void (*)(void *))vm_irq_free, irq) < 0) {
		vm_irq_free(irq);
		return NULL;
	}

	return irq;
}

/**
 * vm_irq_set_level - assert or deassert the virtual interrupt line.
 * @irq: the interrupt line instance.
 * @level: the logical voltage level (1 for high, 0 for low).
 *
 * Triggers the registered callback immediately if the line is valid.
 * This simulates the electrical signaling of a hardware IRQ pin.
 */
void vm_irq_set_level(struct vm_irq *irq, int level)
{
	if (irq && irq->cb)
		irq->cb(irq->data, level);
}