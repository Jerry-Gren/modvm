/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>

#include <modvm/irq.h>

struct vm_irq {
	vm_irq_handler_fn handler;
	void *opaque;
};

struct vm_irq *vm_irq_allocate(vm_irq_handler_fn handler, void *opaque)
{
	struct vm_irq *irq;

	irq = calloc(1, sizeof(*irq));
	if (!irq)
		return NULL;

	irq->handler = handler;
	irq->opaque = opaque;

	return irq;
}

void vm_irq_set(struct vm_irq *irq, int level)
{
	if (irq && irq->handler)
		irq->handler(irq->opaque, level);
}

void vm_irq_free(struct vm_irq *irq)
{
	if (irq)
		free(irq);
}