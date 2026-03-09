/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_IRQ_H
#define MODVM_IRQ_H

struct vm_irq;

/**
 * typedef vm_irq_handler_fn - callback invoked when the IRQ line level changes
 * @opaque: contextual data for the receiver (e.g., the interrupt controller)
 * @level: the logical voltage level (1 for asserted, 0 for deasserted)
 */
typedef void (*vm_irq_handler_fn)(void *opaque, int level);

/**
 * vm_irq_allocate - create a new interrupt line
 * @handler: the receiver's callback function
 * @opaque: the receiver's context
 *
 * Return: A dynamically allocated interrupt line object.
 */
struct vm_irq *vm_irq_allocate(vm_irq_handler_fn handler, void *opaque);

/**
 * vm_irq_set - drive the interrupt line to a specific level
 * @irq: the interrupt line object
 * @level: the target logical level
 *
 * This is called by the device frontend (e.g., UART) to signal an event.
 */
void vm_irq_set(struct vm_irq *irq, int level);

void vm_irq_free(struct vm_irq *irq);

#endif /* MODVM_IRQ_H */