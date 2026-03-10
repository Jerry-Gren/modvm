/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>

#include <modvm/core/interrupt_line.h>

struct vm_interrupt_line {
	vm_interrupt_callback_t callback;
	void *context_data;
};

/**
 * vm_interrupt_line_allocate - allocate a new virtual interrupt line
 * @callback: the function to execute when the line's voltage level changes
 * @context_data: opaque pointer passed to the callback function
 *
 * return: a pointer to the newly allocated interrupt line, or NULL on memory exhaustion.
 */
struct vm_interrupt_line *
vm_interrupt_line_allocate(vm_interrupt_callback_t callback, void *context_data)
{
	struct vm_interrupt_line *line;

	line = calloc(1, sizeof(*line));
	if (!line)
		return NULL;

	line->callback = callback;
	line->context_data = context_data;

	return line;
}

/**
 * vm_interrupt_line_set_level - assert or deassert the virtual interrupt line
 * @line: the interrupt line instance
 * @level: the logical voltage level (1 for high/asserted, 0 for low/deasserted)
 *
 * Triggers the registered callback immediately if the line is valid.
 * This simulates the electrical signaling of a hardware IRQ pin.
 */
void vm_interrupt_line_set_level(struct vm_interrupt_line *line, int level)
{
	if (line && line->callback)
		line->callback(line->context_data, level);
}

/**
 * vm_interrupt_line_free - destroy an interrupt line and release its resources
 * @line: the interrupt line to free
 */
void vm_interrupt_line_free(struct vm_interrupt_line *line)
{
	free(line);
}