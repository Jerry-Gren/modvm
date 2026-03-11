/* SPDX-License-Identifier: GPL-2.0 */
#include <poll.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <modvm/os/event_loop.h>
#include <modvm/core/machine.h>
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

static void posix_event_loop_close(void *data)
{
	struct posix_event_loop *loop = data;

	if (loop->wakeup_pipe[0] != -1)
		close(loop->wakeup_pipe[0]);
	if (loop->wakeup_pipe[1] != -1)
		close(loop->wakeup_pipe[1]);
}

/**
 * vm_event_loop_init - initialize the global asynchronous event dispatcher.
 * @machine: the machine instance
 *
 * Establishes the self-pipe trick. This pipe allows other threads to safely
 * interrupt the blocking poll() system call when system state changes.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_event_loop_init(struct vm_machine *machine)
{
	struct posix_event_loop *loop;
	int flags;

	if (WARN_ON(!machine))
		return -EINVAL;

	loop = vm_machm_zalloc(machine, sizeof(*loop));
	if (!loop)
		return -ENOMEM;

	loop->wakeup_pipe[0] = -1;
	loop->wakeup_pipe[1] = -1;

	if (pipe(loop->wakeup_pipe) < 0) {
		pr_err("failed to create wakeup pipe: %d\n", errno);
		return -errno;
	}

	vm_machm_add_action(machine, posix_event_loop_close, loop);

	flags = fcntl(loop->wakeup_pipe[0], F_GETFL, 0);
	fcntl(loop->wakeup_pipe[0], F_SETFL, flags | O_NONBLOCK);

	flags = fcntl(loop->wakeup_pipe[1], F_GETFL, 0);
	fcntl(loop->wakeup_pipe[1], F_SETFL, flags | O_NONBLOCK);

	machine->event_loop.priv = loop;

	return vm_event_loop_add_fd(machine, loop->wakeup_pipe[0],
				    VM_EVENT_READ, wakeup_pipe_handler, loop);
}

/**
 * vm_event_loop_add_fd - register a host descriptor for monitoring.
 * @machine: the machine instance
 * @fd: the POSIX file descriptor to monitor.
 * @events_mask: bitmask of events (READ, WRITE, ERROR) to wait for.
 * @cb: the function to execute when the condition is met.
 * @data: opaque data passed back to the callback.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_event_loop_add_fd(struct vm_machine *machine, int fd,
			 uint32_t events_mask, vm_event_cb_t cb, void *data)
{
	struct posix_event_loop *loop = machine->event_loop.priv;
	short poll_ev = 0;

	if (WARN_ON(!loop || loop->nr_events >= MAX_POLL_EVENTS))
		return -ENOSPC;

	if (events_mask & VM_EVENT_READ)
		poll_ev |= POLLIN;
	if (events_mask & VM_EVENT_WRITE)
		poll_ev |= POLLOUT;

	loop->poll_fds[loop->nr_events].fd = fd;
	loop->poll_fds[loop->nr_events].events = poll_ev;
	loop->poll_fds[loop->nr_events].revents = 0;

	loop->events[loop->nr_events].fd = fd;
	loop->events[loop->nr_events].cb = cb;
	loop->events[loop->nr_events].data = data;

	loop->nr_events++;
	pr_debug("monitoring fd %d\n", fd);

	return 0;
}

/**
 * vm_event_loop_rm_fd - stop monitoring a host descriptor.
 * @machine: the machine instance
 * @fd: the descriptor to remove from the watch list.
 */
void vm_event_loop_rm_fd(struct vm_machine *machine, int fd)
{
	struct posix_event_loop *loop = machine->event_loop.priv;
	int i, j;

	if (!loop)
		return;

	for (i = 0; i < loop->nr_events; i++) {
		if (loop->poll_fds[i].fd == fd) {
			for (j = i; j < loop->nr_events - 1; j++) {
				loop->poll_fds[j] = loop->poll_fds[j + 1];
				loop->events[j] = loop->events[j + 1];
			}
			loop->nr_events--;
			pr_debug("removed fd %d\n", fd);
			return;
		}
	}
}

/**
 * vm_event_loop_run - start the blocking dispatch loop.
 * @machine: the machine instance
 *
 * Takes over the calling thread and blocks indefinitely, routing I/O 
 * events to their respective callbacks. Exits only on stop request.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_event_loop_run(struct vm_machine *machine)
{
	struct posix_event_loop *loop = machine->event_loop.priv;
	uint32_t revents;
	int ret, i;

	if (!loop)
		return -EINVAL;

	loop->is_running = true;

	while (loop->is_running) {
		ret = poll(loop->poll_fds, loop->nr_events, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			pr_err("poll failed: %d\n", errno);
			return -errno;
		}

		for (i = 0; i < loop->nr_events; i++) {
			if (!loop->poll_fds[i].revents)
				continue;

			revents = 0;
			if (loop->poll_fds[i].revents & (POLLIN | POLLHUP))
				revents |= VM_EVENT_READ;
			if (loop->poll_fds[i].revents & POLLOUT)
				revents |= VM_EVENT_WRITE;
			if (loop->poll_fds[i].revents & POLLERR)
				revents |= VM_EVENT_ERROR;

			if (loop->events[i].cb)
				loop->events[i].cb(loop->poll_fds[i].fd,
						   revents,
						   loop->events[i].data);

			loop->poll_fds[i].revents = 0;
		}
	}

	return 0;
}

/**
 * vm_event_loop_stop - terminate the dispatch loop.
 * @machine: the machine instance
 *
 * Writes a dummy byte to the self-pipe, safely breaking the poll() call
 * in the main thread and allowing vm_event_loop_run() to return.
 */
void vm_event_loop_stop(struct vm_machine *machine)
{
	struct posix_event_loop *loop = machine->event_loop.priv;

	if (!loop)
		return;

	loop->is_running = false;

	if (loop->wakeup_pipe[1] != -1) {
		if (write(loop->wakeup_pipe[1], "x", 1) < 0) {
			/* Safely ignore EAGAIN if the pipe happens to be full */
		}
	}
}