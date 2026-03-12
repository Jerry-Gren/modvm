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

	if (WARN_ON(!ctx || !ctx->event_loop.priv))
		return -EINVAL;

	loop = ctx->event_loop.priv;

	if (WARN_ON(loop->nr_events >= MAX_POLL_EVENTS))
		return -ENOSPC;

	if (events_mask & MODVM_EVENT_READ)
		poll_ev |= POLLIN;
	if (events_mask & MODVM_EVENT_WRITE)
		poll_ev |= POLLOUT;

	loop->poll_fds[loop->nr_events].fd = fd;
	loop->poll_fds[loop->nr_events].events = poll_ev;
	loop->poll_fds[loop->nr_events].revents = 0;

	loop->events[loop->nr_events].fd = fd;
	loop->events[loop->nr_events].cb = cb;
	loop->events[loop->nr_events].data = data;

	loop->nr_events++;
	pr_debug("monitoring descriptor %d\n", fd);

	return 0;
}

/**
 * modvm_event_loop_rm_fd - stop monitoring a host descriptor
 * @ctx: the machine context
 * @fd: the descriptor to remove
 */
void modvm_event_loop_rm_fd(struct modvm_ctx *ctx, int fd)
{
	struct posix_event_loop *loop;
	int i;
	int j;

	if (WARN_ON(!ctx || !ctx->event_loop.priv))
		return;

	loop = ctx->event_loop.priv;

	for (i = 0; i < loop->nr_events; i++) {
		if (loop->poll_fds[i].fd == fd) {
			for (j = i; j < loop->nr_events - 1; j++) {
				loop->poll_fds[j] = loop->poll_fds[j + 1];
				loop->events[j] = loop->events[j + 1];
			}
			loop->nr_events--;
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
		ret = poll(loop->poll_fds, loop->nr_events, -1);
		if (unlikely(ret < 0)) {
			if (errno == EINTR)
				continue;
			pr_err("poll multiplexing failure: %d\n", errno);
			return -errno;
		}

		for (i = 0; i < loop->nr_events; i++) {
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