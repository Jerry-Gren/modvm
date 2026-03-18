/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_OS_EVENT_LOOP_H
#define MODVM_OS_EVENT_LOOP_H

#include <stdint.h>
#include <stdbool.h>

struct modvm_ctx;
struct modvm_event_loop;

#define MODVM_EVENT_READ (1U << 0)
#define MODVM_EVENT_WRITE (1U << 1)
#define MODVM_EVENT_ERROR (1U << 2)

/**
 * typedef modvm_event_cb_t - handler invoked when a file descriptor is ready
 * @fd: the host file descriptor that triggered the event
 * @events: bitmask of triggered events
 * @data: pointer to contextual closure data
 */
typedef void (*modvm_event_cb_t)(int fd, uint32_t events, void *data);

int modvm_event_loop_init(struct modvm_ctx *ctx);
int modvm_event_loop_add_fd(struct modvm_event_loop *loop, int fd,
			    uint32_t events, modvm_event_cb_t cb, void *data);
void modvm_event_loop_rm_fd(struct modvm_event_loop *loop, int fd);
int modvm_event_loop_run(struct modvm_ctx *ctx);
void modvm_event_loop_stop(struct modvm_ctx *ctx);

#endif /* MODVM_OS_EVENT_LOOP_H */