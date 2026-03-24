/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/net.h>
#include <modvm/os/event_loop.h>
#include <modvm/utils/cmdline.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "linux_tap: " fmt

#define TAP_MTU_MAX 2048

/**
 * struct modvm_net_linux_ctx - Linux TAP backend private context
 * @fd: file descriptor for the /dev/net/tun interface
 * @mac: locally administered MAC address presented to the guest
 * @loop: ?
 */
struct modvm_net_linux_ctx {
	int fd;
	uint8_t mac[6];
	struct modvm_event_loop *loop;
};

/**
 * modvm_net_linux_write - push an ethernet frame to the host TAP interface
 * @net: abstract network backend instance
 * @buf: frame payload
 * @len: payload length
 *
 * Hot path transmission function. Employs non-blocking I/O.
 *
 * Return: number of bytes successfully written, or a negative error code.
 */
static ssize_t modvm_net_linux_write(struct modvm_net *net, const uint8_t *buf,
				     size_t len)
{
	struct modvm_net_linux_ctx *ctx = net->priv;
	ssize_t ret;

	if (unlikely(len == 0))
		return 0;

	ret = write(ctx->fd, buf, len);
	if (unlikely(ret < 0)) {
		if (likely(errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;
		return -errno;
	}

	return ret;
}

/**
 * modvm_net_linux_rx_cb - event loop callback triggered upon host frame arrival
 * @fd: the TAP file descriptor
 * @events: poll event bitmask
 * @data: closure payload routing back to the abstract network backend
 *
 * Hot path reception routine. Extracts raw ethernet frames and dispatches
 * them to the frontend device driver (Virtio-Net).
 */
static void modvm_net_linux_rx_cb(int fd, uint32_t events, void *data)
{
	struct modvm_net *net = data;
	uint8_t buf[TAP_MTU_MAX];
	ssize_t ret;

	if (unlikely(!(events & MODVM_EVENT_READ)))
		return;

	ret = read(fd, buf, sizeof(buf));
	if (unlikely(ret <= 0))
		return;

	if (likely(net->rx_cb))
		net->rx_cb(net->rx_data, buf, (size_t)ret);
}

static void modvm_net_linux_set_rx_cb(struct modvm_net *net,
				      struct modvm_event_loop *loop,
				      modvm_net_rx_cb_t cb, void *data)
{
	struct modvm_net_linux_ctx *tctx = net->priv;

	tctx->loop = loop;
	(void)data;

	if (cb) {
		modvm_event_loop_add_fd(loop, tctx->fd, MODVM_EVENT_READ,
					modvm_net_linux_rx_cb, net);
	} else {
		modvm_event_loop_rm_fd(loop, tctx->fd);
	}
}

static int modvm_net_linux_get_mac(struct modvm_net *net, uint8_t mac_out[6])
{
	struct modvm_net_linux_ctx *ctx = net->priv;

	memcpy(mac_out, ctx->mac, 6);
	return 0;
}

static void modvm_net_linux_release(struct modvm_net *net)
{
	struct modvm_net_linux_ctx *ctx;

	if (WARN_ON(!net))
		return;

	ctx = net->priv;
	if (ctx->fd >= 0)
		close(ctx->fd);

	free(ctx);
	free(net);
}

static const struct modvm_net_ops modvm_net_linux_ops = {
	.write = modvm_net_linux_write,
	.set_rx_cb = modvm_net_linux_set_rx_cb,
	.get_mac = modvm_net_linux_get_mac,
	.release = modvm_net_linux_release,
};

/**
 * modvm_net_linux_create - instantiate a Linux TAP interface backend
 * @ifname: requested interface name (e.g., "tap0"), or NULL for auto-allocation
 *
 * Utilizes IFF_NO_PI to strip the legacy 4-byte packet information header,
 * ensuring the interface purely handles bare ethernet frames required by Virtio.
 *
 * Return: allocated network backend pointer, or NULL on failure.
 */
static struct modvm_net *modvm_net_linux_create(const char *opts)
{
	struct modvm_net *net;
	struct modvm_net_linux_ctx *ctx;
	struct ifreq ifr;
	char *ifname;
	int fd;
	int flags;

	ifname = cmdline_extract_opt(opts, "ifname");

	net = calloc(1, sizeof(*net));
	ctx = calloc(1, sizeof(*ctx));
	if (!net || !ctx) {
		free(ifname);
		free(net);
		free(ctx);
		return NULL;
	}

	fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		pr_err("failed to open /dev/net/tun: %d\n", errno);
		goto err_free;
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	if (ifname && strlen(ifname) > 0)
		strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

	if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
		pr_err("failed to allocate tap interface: %d\n", errno);
		goto err_close;
	}

	flags = fcntl(fd, F_GETFL, 0);
	if (flags != -1)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	ctx->fd = fd;
	ctx->mac[0] = 0x52;
	ctx->mac[1] = 0x54;
	ctx->mac[2] = 0x00;
	ctx->mac[3] = 0x12;
	ctx->mac[4] = 0x34;
	ctx->mac[5] = 0x56;

	net->name = "linux-tap";
	net->ops = &modvm_net_linux_ops;
	net->priv = ctx;

	pr_info("attached to host tap interface '%s'\n", ifr.ifr_name);
	free(ifname);
	return net;

err_close:
	close(fd);
err_free:
	free(ifname);
	free(ctx);
	free(net);
	return NULL;
}

static const struct modvm_net_driver linux_tap_driver = {
	.name = "linux-tap",
	.create = modvm_net_linux_create,
};

static void __attribute__((constructor)) modvm_net_linux_register(void)
{
	modvm_net_driver_register(&linux_tap_driver);
}