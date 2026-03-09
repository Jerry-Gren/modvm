/* SPDX-License-Identifier: GPL-2.0 */
#include <poll.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <modvm/event_loop.h>
#include <modvm/log.h>
#include <modvm/bug.h>

/* Pre-allocate a reasonable upper bound for host file descriptors */
#define MAX_EVENTS 64

struct event_record {
	int fd;
	event_callback_fn cb;
	void *opaque;
};

static struct pollfd poll_fds[MAX_EVENTS];
static struct event_record event_records[MAX_EVENTS];
static int num_events = 0;
static bool loop_running = false;

/* File descriptors for the self-pipe trick */
static int wakeup_pipe[2] = { -1, -1 };

/**
 * wakeup_handler - silently discard the wakeup bytes
 *
 * This prevents the level-triggered poll() from firing continuously.
 */
static void wakeup_handler(int fd, uint32_t events, void *opaque)
{
	char dummy[16];
	ssize_t ret;

	(void)events;
	(void)opaque;

	do {
		ret = read(fd, dummy, sizeof(dummy));
	} while (ret > 0);
}

int event_loop_init(void)
{
	int flags;

	if (wakeup_pipe[0] != -1)
		return 0; /* Already initialized */

	if (pipe(wakeup_pipe) < 0) {
		pr_err("Failed to create event loop wakeup pipe: %d\n", errno);
		return -errno;
	}

	/* Configure read end as non-blocking */
	flags = fcntl(wakeup_pipe[0], F_GETFL, 0);
	fcntl(wakeup_pipe[0], F_SETFL, flags | O_NONBLOCK);

	/* Configure write end as non-blocking */
	flags = fcntl(wakeup_pipe[1], F_GETFL, 0);
	fcntl(wakeup_pipe[1], F_SETFL, flags | O_NONBLOCK);

	/* Register the read end into our own event loop */
	return event_loop_add_fd(wakeup_pipe[0], VM_EVENT_READ, wakeup_handler,
				 NULL);
}

int event_loop_add_fd(int fd, uint32_t events, event_callback_fn cb,
		      void *opaque)
{
	short poll_events = 0;

	if (WARN_ON(num_events >= MAX_EVENTS))
		return -ENOSPC;

	if (events & VM_EVENT_READ)
		poll_events |= POLLIN;
	if (events & VM_EVENT_WRITE)
		poll_events |= POLLOUT;

	poll_fds[num_events].fd = fd;
	poll_fds[num_events].events = poll_events;
	poll_fds[num_events].revents = 0;

	event_records[num_events].fd = fd;
	event_records[num_events].cb = cb;
	event_records[num_events].opaque = opaque;

	num_events++;
	pr_debug("Event loop: monitoring fd %d\n", fd);

	return 0;
}

void event_loop_remove_fd(int fd)
{
	int i;
	int j;

	for (i = 0; i < num_events; i++) {
		if (poll_fds[i].fd == fd) {
			/* Shift the remaining arrays to fill the gap */
			for (j = i; j < num_events - 1; j++) {
				poll_fds[j] = poll_fds[j + 1];
				event_records[j] = event_records[j + 1];
			}
			num_events--;
			pr_debug("Event loop: removed fd %d\n", fd);
			return;
		}
	}
}

int event_loop_run(void)
{
	int ret;
	int i;
	uint32_t triggered_events;

	loop_running = true;

	while (loop_running) {
		/*
		 * Block indefinitely until an event occurs or a signal interrupts us.
		 * The timeout is set to -1.
		 */
		ret = poll(poll_fds, num_events, -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			pr_err("Event loop poll failed: %d\n", errno);
			return -errno;
		}

		for (i = 0; i < num_events; i++) {
			if (poll_fds[i].revents == 0)
				continue;

			triggered_events = 0;
			if (poll_fds[i].revents & (POLLIN | POLLHUP))
				triggered_events |= VM_EVENT_READ;
			if (poll_fds[i].revents & POLLOUT)
				triggered_events |= VM_EVENT_WRITE;
			if (poll_fds[i].revents & POLLERR)
				triggered_events |= VM_EVENT_ERROR;

			if (event_records[i].cb) {
				event_records[i].cb(poll_fds[i].fd,
						    triggered_events,
						    event_records[i].opaque);
			}

			/* Clear revents for the next iteration */
			poll_fds[i].revents = 0;
		}
	}

	return 0;
}

void event_loop_stop(void)
{
	loop_running = false;

	/*
	 * The Self-Pipe Trick: Write a dummy byte to the pipe.
	 * This safely and immediately interrupts the blocking poll() system
	 * call in the main thread, forcing it to evaluate the loop_running flag.
	 */
	if (wakeup_pipe[1] != -1) {
		if (write(wakeup_pipe[1], "x", 1) < 0) {
			/* Safely ignore EAGAIN if the pipe happens to be full */
		}
	}
}