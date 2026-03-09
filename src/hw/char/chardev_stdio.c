/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>

#include <modvm/chardev.h>
#include <modvm/event_loop.h>
#include <modvm/log.h>

#undef pr_fmt
#define pr_fmt(fmt) "chardev_stdio: " fmt

struct stdio_backend {
	struct termios old_tio;
	bool termios_saved;
};

static int stdio_write(struct vm_chardev *dev, const uint8_t *buf, size_t len)
{
	ssize_t ret;

	(void)dev;

	/*
	 * Simulate UART transmission over a wire without hardware flow control.
	 * If the host terminal emulator is backpressured and the pipe is full,
	 * the write syscall will return EAGAIN or EWOULDBLOCK.
	 * By ignoring this error, we intentionally drop the payload, accurately
	 * mimicking a UART overrun where electrical signals are lost in the void.
	 * Most importantly, the vCPU thread returns immediately without blocking.
	 */
	ret = write(STDOUT_FILENO, buf, len);
	if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
		/*
		 * Log genuine catastrophic errors, but do not propagate the error
		 * back to the hardware model, as the guest OS expects the transmission
		 * to complete regardless of wire integrity.
		 */
		pr_warn_once("untracked write error %d\n", errno);
	}

	return 0;
}

/**
 * stdio_read_handler - invoked by the global event loop when STDIN has data
 * @fd: the file descriptor (expected to be STDIN_FILENO)
 * @events: bitmask of triggered events
 * @opaque: pointer to the vm_chardev instance
 */
static void stdio_read_handler(int fd, uint32_t events, void *opaque)
{
	struct vm_chardev *dev = opaque;
	uint8_t buf[64];
	ssize_t ret;

	if (!(events & VM_EVENT_READ))
		return;

	/*
	 * Drain the host pipe. Since STDIN is set to non-blocking, this will
	 * return EAGAIN if the buffer is empty, avoiding stalling the event loop.
	 */
	ret = read(fd, buf, sizeof(buf));
	if (ret > 0 && dev->recv_cb) {
		/* Push the payload up to the frontend hardware model */
		dev->recv_cb(dev->recv_opaque, buf, ret);
	}
}

static const struct vm_chardev_ops stdio_ops = {
	.write = stdio_write,
};

struct vm_chardev *chardev_stdio_create(void)
{
	struct vm_chardev *dev;
	struct stdio_backend *backend;
	struct termios new_tio;
	int flags;

	dev = calloc(1, sizeof(*dev));
	backend = calloc(1, sizeof(*backend));
	if (!dev || !backend) {
		free(dev);
		free(backend);
		return NULL;
	}

	dev->name = "stdio";
	dev->ops = &stdio_ops;
	dev->private_data = backend;

	/* Reconfigure standard output for non-blocking asynchronous transmission */
	flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
	if (flags != -1)
		fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK);

	/* Reconfigure standard input for non-blocking asynchronous reception */
	flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	if (flags != -1)
		fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

	/*
	 * Hijack the host terminal settings to disable line buffering and echo.
	 * This allows us to intercept individual keystrokes (like Ctrl+C) and
	 * forward them as raw bytes to the guest operating system.
	 */
	if (tcgetattr(STDIN_FILENO, &backend->old_tio) == 0) {
		backend->termios_saved = true;
		new_tio = backend->old_tio;
		cfmakeraw(&new_tio);
		tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
	}

	/* Bind STDIN to the core hypervisor event loop */
	event_loop_add_fd(STDIN_FILENO, VM_EVENT_READ, stdio_read_handler, dev);

	return dev;
}

void chardev_stdio_destroy(struct vm_chardev *dev)
{
	struct stdio_backend *backend;

	if (!dev)
		return;

	backend = dev->private_data;

	/* Detach from the event loop before releasing memory */
	event_loop_remove_fd(STDIN_FILENO);

	/* Politely restore the user's terminal to its original state */
	if (backend && backend->termios_saved) {
		tcsetattr(STDIN_FILENO, TCSANOW, &backend->old_tio);
	}

	free(backend);
	free(dev);
}