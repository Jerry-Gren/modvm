/* SPDX-License-Identifier: GPL-2.0 */
#include <poll.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <modvm/os/event_loop.h>
#include <modvm/core/modvm.h>
#include <modvm/core/ctxm.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "posix_event_loop: " fmt

#define MAX_POLL_EVENTS 64

struct event_record {
	int fd;
	modvm_event_cb_t cb;
	void *data;
};

struct posix_event_loop {
	struct pollfd poll_fds[MAX_POLL_EVENTS];
	struct event_record events[MAX_POLL_EVENTS];
	int nr_events;
	bool is_running;
	int wakeup_pipe[2];
};

static void wakeup_pipe_handler(int fd, uint32_t revents, void *data)
{
	char buf[16];
	ssize_t ret;

	(void)revents;
	(void)data;

	do {
		ret = read(fd, buf, sizeof(buf));
	} while (ret > 0);
}

static void posix_event_loop_close(struct posix_event_loop *loop)
{
	if (loop->wakeup_pipe[0] != -1)
		close(loop->wakeup_pipe[0]);
	if (loop->wakeup_pipe[1] != -1)
		close(loop->wakeup_pipe[1]);
}

/**
 * event_loop_compact - eliminate tombstones and compress event arrays
 * @loop: the posix event loop structure
 *
 * Performs a garbage collection pass over the polling arrays. It shifts
 * active file descriptors leftwards to overwrite slots marked as deleted.
 * This guarantees a dense array is passed to the kernel, optimizing the
 * poll() system call and preventing descriptor exhaustion.
 */
static void event_loop_compact(struct posix_event_loop *loop)
{
	int i;
	int valid_count = 0;

	for (i = 0; i < loop->nr_events; i++) {
		if (loop->poll_fds[i].fd != -1) {
			if (i != valid_count) {
				loop->poll_fds[valid_count] = loop->poll_fds[i];
				loop->events[valid_count] = loop->events[i];
			}
			valid_count++;
		}
	}

	loop->nr_events = valid_count;
}

/**
 * modvm_event_loop_init - initialize the asynchronous event dispatcher
 * @ctx: the isolated machine context
 *
 * Establishes the self-pipe trick. This pipe allows other threads to safely
 * interrupt the blocking poll() system call when system state changes.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_event_loop_init(struct modvm_ctx *ctx)
{
	struct posix_event_loop *loop;
	int flags;

	if (WARN_ON(!ctx))
		return -EINVAL;

	loop = modvm_ctxm_zalloc(ctx, sizeof(*loop));
	if (!loop)
		return -ENOMEM;

	loop->wakeup_pipe[0] = -1;
	loop->wakeup_pipe[1] = -1;

	if (pipe(loop->wakeup_pipe) < 0) {
		pr_err("failed to instantiate synthetic wakeup pipe: %d\n",
		       errno);
		return -errno;
	}

	modvm_ctxm_add_action(ctx, posix_event_loop_close, loop);

	flags = fcntl(loop->wakeup_pipe[0], F_GETFL, 0);
	fcntl(loop->wakeup_pipe[0], F_SETFL, flags | O_NONBLOCK);

	flags = fcntl(loop->wakeup_pipe[1], F_GETFL, 0);
	fcntl(loop->wakeup_pipe[1], F_SETFL, flags | O_NONBLOCK);

	ctx->event_loop.priv = loop;

	return modvm_event_loop_add_fd(ctx, loop->wakeup_pipe[0],
				       MODVM_EVENT_READ, wakeup_pipe_handler,
				       loop);
}

/**
 * modvm_event_loop_add_fd - register a host descriptor for asynchronous monitoring
 * @ctx: the machine context
 * @fd: the POSIX file descriptor
 * @events_mask: bitmask of events to wait for
 * @cb: the function to execute
 * @data: opaque closure passed back to the callback
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_event_loop_add_fd(struct modvm_ctx *ctx, int fd, uint32_t events_mask,
			    modvm_event_cb_t cb, void *data)
{
	struct posix_event_loop *loop;
	short poll_ev = 0;
	int slot = -1;
	int i;

	if (WARN_ON(!ctx || !ctx->event_loop.priv))
		return -EINVAL;

	loop = ctx->event_loop.priv;

	if (events_mask & MODVM_EVENT_READ)
		poll_ev |= POLLIN;
	if (events_mask & MODVM_EVENT_WRITE)
		poll_ev |= POLLOUT;

	/* Scan for a tombstone (soft-deleted) slot to reuse */
	for (i = 0; i < loop->nr_events; i++) {
		if (loop->poll_fds[i].fd == -1) {
			slot = i;
			break;
		}
	}

	/* Append to the end if no free slots exist */
	if (slot == -1) {
		if (WARN_ON(loop->nr_events >= MAX_POLL_EVENTS))
			return -ENOSPC;
		slot = loop->nr_events++;
	}

	loop->poll_fds[slot].fd = fd;
	loop->poll_fds[slot].events = poll_ev;
	loop->poll_fds[slot].revents = 0;

	loop->events[slot].fd = fd;
	loop->events[slot].cb = cb;
	loop->events[slot].data = data;

	pr_debug("monitoring descriptor %d at slot %d\n", fd, slot);

	return 0;
}

/**
 * modvm_event_loop_rm_fd - stop monitoring a host descriptor
 * @ctx: the machine context
 * @fd: the descriptor to remove
 *
 * Utilizes a tombstone (soft-delete) mechanism by setting the descriptor
 * to -1. This explicitly prevents array shifting during active event
 * dispatching, solving the iterator collapse vulnerability.
 */
void modvm_event_loop_rm_fd(struct modvm_ctx *ctx, int fd)
{
	struct posix_event_loop *loop;
	int i;

	if (WARN_ON(!ctx || !ctx->event_loop.priv))
		return;

	loop = ctx->event_loop.priv;

	for (i = 0; i < loop->nr_events; i++) {
		if (loop->poll_fds[i].fd == fd) {
			/* * Soft-delete. POSIX poll() will natively ignore fds < 0.
             * The slot will be collected during the next compaction pass.
             */
			loop->poll_fds[i].fd = -1;
			loop->poll_fds[i].events = 0;
			loop->poll_fds[i].revents = 0;

			loop->events[i].fd = -1;
			loop->events[i].cb = NULL;
			loop->events[i].data = NULL;

			pr_debug("relinquished monitoring of descriptor %d\n",
				 fd);
			return;
		}
	}
}

/**
 * modvm_event_loop_run - block indefinitely and route IO multiplexing
 * @ctx: the machine context
 *
 * Return: 0 on exit request, or a negative error code.
 */
int modvm_event_loop_run(struct modvm_ctx *ctx)
{
	struct posix_event_loop *loop;
	uint32_t revents;
	int ret;
	int i;

	if (WARN_ON(!ctx || !ctx->event_loop.priv))
		return -EINVAL;

	loop = ctx->event_loop.priv;
	loop->is_running = true;

	while (loop->is_running) {
		/* Compress the array outside the dispatching phase */
		event_loop_compact(loop);

		ret = poll(loop->poll_fds, loop->nr_events, -1);
		if (unlikely(ret < 0)) {
			if (errno == EINTR)
				continue;
			pr_err("poll multiplexing failure: %d\n", errno);
			return -errno;
		}

		for (i = 0; i < loop->nr_events; i++) {
			/*
			 * Skip if a previous callback in this loop iteration 
			 * removed this descriptor (soft-delete).
			 */
			if (unlikely(loop->poll_fds[i].fd == -1))
				continue;

			if (!loop->poll_fds[i].revents)
				continue;

			revents = 0;
			if (loop->poll_fds[i].revents & (POLLIN | POLLHUP))
				revents |= MODVM_EVENT_READ;
			if (loop->poll_fds[i].revents & POLLOUT)
				revents |= MODVM_EVENT_WRITE;
			if (loop->poll_fds[i].revents & POLLERR)
				revents |= MODVM_EVENT_ERROR;

			if (likely(loop->events[i].cb))
				loop->events[i].cb(loop->poll_fds[i].fd,
						   revents,
						   loop->events[i].data);

			loop->poll_fds[i].revents = 0;
		}
	}

	return 0;
}

/**
 * modvm_event_loop_stop - safely break the blocking dispatcher
 * @ctx: the machine context
 */
void modvm_event_loop_stop(struct modvm_ctx *ctx)
{
	struct posix_event_loop *loop;

	if (WARN_ON(!ctx || !ctx->event_loop.priv))
		return;

	loop = ctx->event_loop.priv;
	loop->is_running = false;

	if (likely(loop->wakeup_pipe[1] != -1)) {
		if (write(loop->wakeup_pipe[1], "x", 1) < 0) {
			/* EAGAIN is safely ignored if the pipe is saturated */
		}
	}
}