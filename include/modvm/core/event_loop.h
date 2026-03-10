/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_EVENT_LOOP_H
#define MODVM_CORE_EVENT_LOOP_H

#include <stdint.h>
#include <stdbool.h>

#define VM_EVENT_READ 0x01
#define VM_EVENT_WRITE 0x02
#define VM_EVENT_ERROR 0x04

/**
 * typedef vm_event_callback_t - Handler invoked when a file descriptor is ready.
 * @fd: The host file descriptor that triggered the event.
 * @events: Bitmask of triggered events (VM_EVENT_READ, etc.).
 * @data: Pointer to contextual data provided during registration.
 */
typedef void (*vm_event_callback_t)(int fd, uint32_t events, void *data);

int vm_event_loop_init(void);

int vm_event_loop_add_file_descriptor(int fd, uint32_t events,
				      vm_event_callback_t cb, void *data);

void vm_event_loop_remove_file_descriptor(int fd);

int vm_event_loop_run(void);

void vm_event_loop_stop(void);

#endif /* MODVM_CORE_EVENT_LOOP_H */