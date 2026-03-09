/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_EVENT_LOOP_H
#define MODVM_EVENT_LOOP_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Event bitmasks mirroring POSIX poll standard, 
 * decoupled to prevent OS header leakage into the core abstraction.
 */
#define VM_EVENT_READ 0x01
#define VM_EVENT_WRITE 0x02
#define VM_EVENT_ERROR 0x04

/**
 * typedef event_callback_fn - handler invoked when a file descriptor is ready
 * @fd: the file descriptor that triggered the event
 * @events: the bitmask of triggered events
 * @opaque: pointer to the contextual data provided during registration
 */
typedef void (*event_callback_fn)(int fd, uint32_t events, void *opaque);

int event_loop_init(void);

/**
 * event_loop_add_fd - register a file descriptor for asynchronous monitoring
 * @fd: the host file descriptor
 * @events: bitmask of events to watch (e.g., VM_EVENT_READ)
 * @cb: function to invoke when events occur
 * @opaque: contextual data passed back to the callback
 *
 * Return: 0 on success, negative error code on failure.
 */
int event_loop_add_fd(int fd, uint32_t events, event_callback_fn cb,
		      void *opaque);

/**
 * event_loop_remove_fd - stop monitoring a file descriptor
 * @fd: the host file descriptor to remove
 */
void event_loop_remove_fd(int fd);

/**
 * event_loop_run - enter the blocking event dispatch loop
 *
 * This function takes over the calling thread and blocks indefinitely,
 * dispatching I/O events to registered callbacks until event_loop_stop is called.
 */
int event_loop_run(void);

/**
 * event_loop_stop - signal the event loop to terminate gracefully
 */
void event_loop_stop(void);

#endif /* MODVM_EVENT_LOOP_H */