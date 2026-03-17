/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>

#include <modvm/core/chardev.h>
#include <modvm/core/modvm.h>
#include <modvm/os/event_loop.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "posix_stdio: " fmt

#define STDIO_TX_BUF_SIZE 256
#define ESCAPE_CHAR 0x01 /* Ctrl+a */

struct stdio_ctx {
	struct termios orig_termios;
	bool is_saved;
	uint8_t tx_buf[STDIO_TX_BUF_SIZE];
	size_t tx_len;

	bool escape_pending;
	struct modvm_ctx *ctx;
};

static int stdio_write(struct modvm_chardev *dev, const uint8_t *buf,
		       size_t len)
{
	struct stdio_ctx *ctx = dev->priv;
	size_t i;

	for (i = 0; i < len; i++) {
		ctx->tx_buf[ctx->tx_len++] = buf[i];

		if (unlikely(ctx->tx_len >= STDIO_TX_BUF_SIZE)) {
			ssize_t ret =
				write(STDOUT_FILENO, ctx->tx_buf, ctx->tx_len);
			if (unlikely(ret < 0 && errno != EAGAIN &&
				     errno != EWOULDBLOCK))
				pr_warn_once(
					"untracked standard output pipe congestion %d\n",
					errno);
			ctx->tx_len = 0;
		}
	}

	if (likely(ctx->tx_len > 0)) {
		ssize_t ret = write(STDOUT_FILENO, ctx->tx_buf, ctx->tx_len);
		if (unlikely(ret < 0 && errno != EAGAIN &&
			     errno != EWOULDBLOCK))
			pr_warn_once(
				"untracked standard output pipe congestion %d\n",
				errno);
		ctx->tx_len = 0;
	}

	return 0;
}

/**
 * stdio_rx_cb - invoked by the global event loop when STDIN data arrives
 * @fd: the STDIN descriptor
 * @events: bitmask of triggered poll events
 * @data: opaque closure pointing to the character device instance
 */
static void stdio_rx_cb(int fd, uint32_t events, void *data)
{
	struct modvm_chardev *dev = data;
	struct stdio_ctx *ctx = dev->priv;
	uint8_t rx_buf[64];
	uint8_t filtered_buf[64];
	size_t filtered_len = 0;
	ssize_t ret;
	ssize_t i;

	if (unlikely(!(events & MODVM_EVENT_READ)))
		return;

	ret = read(fd, rx_buf, sizeof(rx_buf));
	if (unlikely(ret <= 0)) {
		/* catch eof */
		if (ret == 0)
			modvm_event_loop_rm_fd(ctx->ctx, fd);
		return;
	}

	for (i = 0; i < ret; i++) {
		if (unlikely(ctx->escape_pending)) {
			ctx->escape_pending = false;

			if (rx_buf[i] == 'x' || rx_buf[i] == 'X') {
				pr_info("caught Ctrl+a x, requesting shutdown...\n");
				modvm_request_shutdown(ctx->ctx);
				return;
			} else if (rx_buf[i] == ESCAPE_CHAR) {
				/* Double escape sends a literal Ctrl+a to the guest */
				filtered_buf[filtered_len++] = ESCAPE_CHAR;
			} else {
				pr_info("Ctrl+a %c is not supported (only 'x' to exit)\n",
					(rx_buf[i] >= 32 && rx_buf[i] <= 126) ?
						rx_buf[i] :
						'?');
			}
		} else if (unlikely(rx_buf[i] == ESCAPE_CHAR)) {
			ctx->escape_pending = true;
		} else {
			filtered_buf[filtered_len++] = rx_buf[i];
		}
	}

	if (likely(filtered_len > 0 && dev->rx_cb))
		dev->rx_cb(dev->rx_data, filtered_buf, filtered_len);
}

static void stdio_set_rx_cb(struct modvm_chardev *dev, struct modvm_ctx *ctx,
			    modvm_chardev_rx_cb_t cb, void *data)
{
	struct stdio_ctx *sctx = dev->priv;
	sctx->ctx = ctx;

	(void)data;

	if (cb) {
		modvm_event_loop_add_fd(ctx, STDIN_FILENO, MODVM_EVENT_READ,
					stdio_rx_cb, dev);
	} else {
		modvm_event_loop_rm_fd(ctx, STDIN_FILENO);
	}
}

static void stdio_pause_rx(struct modvm_chardev *dev)
{
	struct stdio_ctx *ctx = dev->priv;

	if (likely(ctx->ctx))
		modvm_event_loop_rm_fd(ctx->ctx, STDIN_FILENO);
}

static void stdio_resume_rx(struct modvm_chardev *dev)
{
	struct stdio_ctx *ctx = dev->priv;

	if (likely(ctx->ctx && dev->rx_cb))
		modvm_event_loop_add_fd(ctx->ctx, STDIN_FILENO,
					MODVM_EVENT_READ, stdio_rx_cb, dev);
}

static const struct modvm_chardev_ops stdio_ops = {
	.write = stdio_write,
	.set_rx_cb = stdio_set_rx_cb,
	.pause_rx = stdio_pause_rx,
	.resume_rx = stdio_resume_rx,
};

/**
 * modvm_chardev_stdio_create - hijack host terminal for console input/output
 *
 * Disables canonical text processing on the host terminal to allow the
 * guest operating system full control over character interpretation.
 *
 * Return: an allocated character device object, or NULL on failure.
 */
struct modvm_chardev *modvm_chardev_stdio_create(void)
{
	struct modvm_chardev *dev;
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

	return dev;
}

/**
 * modvm_chardev_stdio_destroy - release terminal and restore standard settings
 * @dev: the character device to tear down
 */
void modvm_chardev_stdio_destroy(struct modvm_chardev *dev)
{
	struct stdio_ctx *ctx;

	if (WARN_ON(!dev))
		return;

	ctx = dev->priv;

	if (ctx->tx_len > 0) {
		if (write(STDOUT_FILENO, ctx->tx_buf, ctx->tx_len) < 0) {
			/*
			 * Best effort flush during teardown. 
			 * Safely ignore errors if the host pipe is already broken or closed.
			 */
		}
	}

	if (ctx->is_saved)
		tcsetattr(STDIN_FILENO, TCSANOW, &ctx->orig_termios);

	free(ctx);
	free(dev);
}