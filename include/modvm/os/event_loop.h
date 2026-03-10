/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_EVENT_LOOP_H
#define MODVM_CORE_EVENT_LOOP_H

#include <stdint.h>
#include <stdbool.h>

#define VM_EVENT_READ (1U << 0)
#define VM_EVENT_WRITE (1U << 1)
#define VM_EVENT_ERROR (1U << 2)

/**
 * typedef vm_event_cb_t - Handler invoked when a file descriptor is ready.
 * @fd: The host file descriptor that triggered the event.
 * @events: Bitmask of triggered events.
 * @data: Pointer to contextual data provided during registration.
 */
typedef void (*vm_event_cb_t)(int fd, uint32_t events, void *data);

int vm_event_loop_init(void);

int vm_event_loop_add_fd(int fd, uint32_t events, vm_event_cb_t cb, void *data);

void vm_event_loop_rm_fd(int fd);

int vm_event_loop_run(void);

void vm_event_loop_stop(void);

#endif /* MODVM_CORE_EVENT_LOOP_H */