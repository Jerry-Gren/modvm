/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>

#include <modvm/core/chardev.h>
#include <modvm/core/event_loop.h>
#include <modvm/utils/log.h>

#undef pr_fmt
#define pr_fmt(fmt) "posix_stdio: " fmt

#define STDIO_TX_BUF_SIZE 256

struct stdio_ctx {
	struct termios orig_termios;
	bool is_saved;
	uint8_t tx_buf[STDIO_TX_BUF_SIZE];
	size_t tx_len;
};

static int stdio_write(struct vm_chardev *dev, const uint8_t *buf, size_t len)
{
	struct stdio_ctx *ctx = dev->priv;
	size_t i;

	for (i = 0; i < len; i++) {
		ctx->tx_buf[ctx->tx_len++] = buf[i];

		if (buf[i] == '\n' || ctx->tx_len >= STDIO_TX_BUF_SIZE) {
			/*
			 * simulate transmission over a wire without hardware flow control.
			 * If the host terminal emulator is backpressured, we intentionally
			 * drop the payload to accurately mimic an electrical overrun.
			 */
			ssize_t ret =
				write(STDOUT_FILENO, ctx->tx_buf, ctx->tx_len);

			if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
				pr_warn_once(
					"untracked standard output write error %d\n",
					errno);

			ctx->tx_len = 0;
		}
	}

	return 0;
}

/**
 * stdio_rx_cb - invoked by the global event loop when STDIN has data.
 * @fd: the STDIN file descriptor.
 * @events: bitmask of triggered poll events.
 * @data: pointer to the character device instance.
 */
static void stdio_rx_cb(int fd, uint32_t events, void *data)
{
	struct vm_chardev *dev = data;
	uint8_t rx_buf[64];
	ssize_t ret;

	if (!(events & VM_EVENT_READ))
		return;

	ret = read(fd, rx_buf, sizeof(rx_buf));
	if (ret > 0 && dev->rx_cb)
		dev->rx_cb(dev->rx_data, rx_buf, ret);
}

static const struct vm_chardev_ops stdio_ops = {
	.write = stdio_write,
};

/**
 * vm_chardev_stdio_create - hijack host terminal for guest serial output.
 *
 * Configures the host standard input/output to operate in non-blocking
 * raw mode, preventing the host OS from interpreting special characters.
 *
 * return: an allocated character device object, or NULL on failure.
 */
struct vm_chardev *vm_chardev_stdio_create(void)
{
	struct vm_chardev *dev;
	struct stdio_ctx *ctx;
	struct termios raw;
	int flags;

	dev = calloc(1, sizeof(*dev));
	ctx = calloc(1, sizeof(*ctx));
	if (!dev || !ctx) {
		free(dev);
		free(ctx);
		return NULL;
	}

	dev->name = "stdio";
	dev->ops = &stdio_ops;
	dev->priv = ctx;

	flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
	if (flags != -1)
		fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK);

	flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	if (flags != -1)
		fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

	if (tcgetattr(STDIN_FILENO, &ctx->orig_termios) == 0) {
		ctx->is_saved = true;
		raw = ctx->orig_termios;
		cfmakeraw(&raw);
		tcsetattr(STDIN_FILENO, TCSANOW, &raw);
	}

	vm_event_loop_add_fd(STDIN_FILENO, VM_EVENT_READ, stdio_rx_cb, dev);

	return dev;
}

/**
 * vm_chardev_stdio_destroy - release terminal and restore host settings.
 * @dev: The character device to tear down.
 */
void vm_chardev_stdio_destroy(struct vm_chardev *dev)
{
	struct stdio_ctx *ctx;

	if (!dev)
		return;

	ctx = dev->priv;

	/* flush if buffer is not empty when exit */
	if (ctx->tx_len > 0)
		write(STDOUT_FILENO, ctx->tx_buf, ctx->tx_len);

	vm_event_loop_rm_fd(STDIN_FILENO);

	if (ctx->is_saved)
		tcsetattr(STDIN_FILENO, TCSANOW, &ctx->orig_termios);

	free(ctx);
	free(dev);
}