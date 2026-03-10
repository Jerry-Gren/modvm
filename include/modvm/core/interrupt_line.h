/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_INTERRUPT_LINE_H
#define MODVM_CORE_INTERRUPT_LINE_H

struct vm_interrupt_line;

/**
 * typedef vm_interrupt_callback_t - callback invoked upon interrupt state change
 * @data: contextual data pointer provided during allocation
 * @level: the logical voltage level representing the line state (0 or 1)
 */
typedef void (*vm_interrupt_callback_t)(void *data, int level);

struct vm_interrupt_line *
vm_interrupt_line_allocate(vm_interrupt_callback_t handler, void *data);

void vm_interrupt_line_set_level(struct vm_interrupt_line *line, int level);

void vm_interrupt_line_free(struct vm_interrupt_line *line);

#endif /* MODVM_CORE_INTERRUPT_LINE_H */