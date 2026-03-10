/* SPDX-License-Identifier: GPL-2.0 */
#include <poll.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <modvm/core/event_loop.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>

#undef pr_fmt
#define pr_fmt(fmt) "posix_event_loop: " fmt

#define MAX_POLL_EVENTS 64

struct event_record {
	int fd;
	vm_event_cb_t cb;
	void *data;
};

static struct pollfd poll_fds[MAX_POLL_EVENTS];
static struct event_record events[MAX_POLL_EVENTS];
static int nr_events;
static bool is_running;

/* File descriptors for the self-pipe trick used to break the poll loop */
static int wakeup_pipe[2] = { -1, -1 };

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

/**
 * vm_event_loop_init - initialize the global asynchronous event dispatcher.
 *
 * Establishes the self-pipe trick. This pipe allows other threads to safely
 * interrupt the blocking poll() system call when system state changes.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_event_loop_init(void)
{
	int flags;

	if (wakeup_pipe[0] != -1)
		return 0;

	if (pipe(wakeup_pipe) < 0) {
		pr_err("failed to create wakeup pipe: %d\n", errno);
		return -errno;
	}

	flags = fcntl(wakeup_pipe[0], F_GETFL, 0);
	fcntl(wakeup_pipe[0], F_SETFL, flags | O_NONBLOCK);

	flags = fcntl(wakeup_pipe[1], F_GETFL, 0);
	fcntl(wakeup_pipe[1], F_SETFL, flags | O_NONBLOCK);

	return vm_event_loop_add_fd(wakeup_pipe[0], VM_EVENT_READ,
				    wakeup_pipe_handler, NULL);
}

/**
 * vm_event_loop_add_fd - register a host descriptor for monitoring.
 * @fd: the POSIX file descriptor to monitor.
 * @events_mask: bitmask of events (READ, WRITE, ERROR) to wait for.
 * @cb: the function to execute when the condition is met.
 * @data: opaque data passed back to the callback.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_event_loop_add_fd(int fd, uint32_t events_mask, vm_event_cb_t cb,
			 void *data)
{
	short poll_ev = 0;

	if (WARN_ON(nr_events >= MAX_POLL_EVENTS))
		return -ENOSPC;

	if (events_mask & VM_EVENT_READ)
		poll_ev |= POLLIN;
	if (events_mask & VM_EVENT_WRITE)
		poll_ev |= POLLOUT;

	poll_fds[nr_events].fd = fd;
	poll_fds[nr_events].events = poll_ev;
	poll_fds[nr_events].revents = 0;

	events[nr_events].fd = fd;
	events[nr_events].cb = cb;
	events[nr_events].data = data;

	nr_events++;
	pr_debug("monitoring fd %d\n", fd);

	return 0;
}

/**
 * vm_event_loop_rm_fd - stop monitoring a host descriptor.
 * @fd: the descriptor to remove from the watch list.
 */
void vm_event_loop_rm_fd(int fd)
{
	int i, j;

	for (i = 0; i < nr_events; i++) {
		if (poll_fds[i].fd == fd) {
			for (j = i; j < nr_events - 1; j++) {
				poll_fds[j] = poll_fds[j + 1];
				events[j] = events[j + 1];
			}
			nr_events--;
			pr_debug("removed fd %d\n", fd);
			return;
		}
	}
}

/**
 * vm_event_loop_run - start the blocking dispatch loop.
 *
 * Takes over the calling thread and blocks indefinitely, routing I/O 
 * events to their respective callbacks. Exits only on stop request.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_event_loop_run(void)
{
	uint32_t revents;
	int ret, i;

	is_running = true;

	while (is_running) {
		ret = poll(poll_fds, nr_events, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			pr_err("poll failed: %d\n", errno);
			return -errno;
		}

		for (i = 0; i < nr_events; i++) {
			if (!poll_fds[i].revents)
				continue;

			revents = 0;
			if (poll_fds[i].revents & (POLLIN | POLLHUP))
				revents |= VM_EVENT_READ;
			if (poll_fds[i].revents & POLLOUT)
				revents |= VM_EVENT_WRITE;
			if (poll_fds[i].revents & POLLERR)
				revents |= VM_EVENT_ERROR;

			if (events[i].cb)
				events[i].cb(poll_fds[i].fd, revents,
					     events[i].data);

			poll_fds[i].revents = 0;
		}
	}

	return 0;
}

/**
 * vm_event_loop_stop - terminate the dispatch loop.
 *
 * Writes a dummy byte to the self-pipe, safely breaking the poll() call
 * in the main thread and allowing vm_event_loop_run() to return.
 */
void vm_event_loop_stop(void)
{
	is_running = false;

	if (wakeup_pipe[1] != -1) {
		if (write(wakeup_pipe[1], "x", 1) < 0) {
			/* Safely ignore EAGAIN if the pipe happens to be full */
		}
	}
}