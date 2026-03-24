/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/net.h>
#include <modvm/core/modvm.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_net.h>
#include <modvm/utils/byteorder.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

#include "virtqueue.h"
#include "virtio_net_reg.h"

#undef pr_fmt
#define pr_fmt(fmt) "virtio_net: " fmt

#define VIRTIO_ID_NET 1
#define VIRTIO_NET_QUEUE_SIZE 256
#define VIRTIO_NET_CTRL_QUEUE_SIZE 64

struct virtio_net_ctx {
	struct virtio_device vdev;
	struct modvm_net *backend;
	struct virtio_net_config config;
};

static void virtio_net_rx_cb(void *data, const uint8_t *buf, size_t len);

/**
 * virtio_net_realize - initialize internal device state and allocate virtqueues
 * @vdev: the base virtio device pointer
 *
 * Return: 0 on success, or a negative error code.
 */
static int virtio_net_realize(struct virtio_device *vdev)
{
	struct virtio_net_ctx *ctx = vdev->priv;
	struct modvm_ctx *mctx = vdev->parent_dev->ctx;

	/*
	 * VQ 0: Receive Queue (Host -> Guest)
	 * VQ 1: Transmit Queue (Guest -> Host)
	 * VQ 2: Control Queue (Management plane)
	 */
	vdev->vqs[0] =
		virtqueue_create(&mctx->accel.mem_space, VIRTIO_NET_QUEUE_SIZE);
	vdev->vqs[1] =
		virtqueue_create(&mctx->accel.mem_space, VIRTIO_NET_QUEUE_SIZE);
	vdev->vqs[2] = virtqueue_create(&mctx->accel.mem_space,
					VIRTIO_NET_CTRL_QUEUE_SIZE);

	if (!vdev->vqs[0] || !vdev->vqs[1] || !vdev->vqs[2])
		return -ENOMEM;

	vdev->nr_vqs = 3;

	memset(&ctx->config, 0, sizeof(ctx->config));
	if (ctx->backend && ctx->backend->ops->get_mac)
		ctx->backend->ops->get_mac(ctx->backend, ctx->config.mac);

	/* Enforce strict little-endian assignment for configuration space */
	ctx->config.status = cpu_to_le16(VIRTIO_NET_S_LINK_UP);

	if (ctx->backend && ctx->backend->ops->set_rx_cb)
		ctx->backend->ops->set_rx_cb(ctx->backend, &mctx->event_loop,
					     virtio_net_rx_cb, ctx);

	pr_info("virtio-net realized with mac %02x:%02x:%02x:%02x:%02x:%02x\n",
		ctx->config.mac[0], ctx->config.mac[1], ctx->config.mac[2],
		ctx->config.mac[3], ctx->config.mac[4], ctx->config.mac[5]);

	return 0;
}

/**
 * virtio_net_unrealize - cleanly teardown local allocations
 * @vdev: the base virtio device pointer
 */
static void virtio_net_unrealize(struct virtio_device *vdev)
{
	struct virtio_net_ctx *ctx = vdev->priv;
	struct modvm_ctx *mctx;
	int i;

	if (WARN_ON(!ctx))
		return;

	mctx = vdev->parent_dev->ctx;

	if (ctx->backend && ctx->backend->ops->set_rx_cb)
		ctx->backend->ops->set_rx_cb(ctx->backend, &mctx->event_loop,
					     NULL, NULL);

	for (i = 0; i < vdev->nr_vqs; i++) {
		if (vdev->vqs[i])
			virtqueue_destroy(vdev->vqs[i]);
	}

	free(ctx);
}

static uint64_t virtio_net_get_features(struct virtio_device *vdev)
{
	(void)vdev;
	return (1ULL << VIRTIO_NET_F_MAC) | (1ULL << VIRTIO_NET_F_STATUS) |
	       (1ULL << VIRTIO_NET_F_CTRL_VQ) |
	       (1ULL << VIRTIO_NET_F_CTRL_MAC_ADDR);
}

static uint64_t virtio_net_read_config(struct virtio_device *vdev,
				       uint64_t offset, uint8_t size)
{
	struct virtio_net_ctx *ctx = vdev->priv;
	uint64_t val = 0;

	if (WARN_ON(offset + size > sizeof(ctx->config)))
		return 0;

	memcpy(&val, (uint8_t *)&ctx->config + offset, size);
	return val;
}

/**
 * virtio_net_handle_ctrl - process management plane commands
 * @vdev: the base virtio device pointer
 * @vq: the control virtqueue instance
 */
static void virtio_net_handle_ctrl(struct virtio_device *vdev,
				   struct virtqueue *vq)
{
	struct virtio_net_ctx *ctx = vdev->priv;
	struct virtqueue_buf bufs[32];
	uint16_t desc_idx;
	int num_bufs;
	bool need_irq = false;

	while ((num_bufs = virtqueue_pop(vq, &desc_idx, bufs, 32)) > 0) {
		struct virtio_net_ctrl_hdr *ctrl;
		uint8_t *status;

		/* Minimal valid chain: Header -> [Data...] -> Status (Ack) */
		if (unlikely(num_bufs < 2 || !bufs[num_bufs - 1].is_write)) {
			virtqueue_push(vq, desc_idx, 0);
			need_irq = true;
			continue;
		}

		ctrl = bufs[0].hva;
		status = bufs[num_bufs - 1].hva;
		*status = VIRTIO_NET_ERR;

		if (unlikely(bufs[0].len < sizeof(*ctrl)))
			goto push_desc;

		if (ctrl->class == VIRTIO_NET_CTRL_MAC) {
			if (ctrl->cmd == VIRTIO_NET_CTRL_MAC_ADDR_SET) {
				if (num_bufs >= 3 && bufs[1].len >= 6) {
					memcpy(ctx->config.mac, bufs[1].hva, 6);
					*status = VIRTIO_NET_OK;
					pr_debug(
						"guest dynamically updated MAC address\n");
				}
			} else if (ctrl->cmd == VIRTIO_NET_CTRL_MAC_TABLE_SET) {
				/* Silently accept multicast table updates */
				*status = VIRTIO_NET_OK;
			}
		} else if (ctrl->class == VIRTIO_NET_CTRL_RX) {
			/* Silently accept promiscuous mode changes (ignored by TAP) */
			*status = VIRTIO_NET_OK;
		}

push_desc:
		/* Notify guest that 1 byte (the status ack) was written */
		virtqueue_push(vq, desc_idx, 1);
		need_irq = true;
	}

	if (likely(need_irq && vdev->transport && vdev->transport->set_irq_cb))
		vdev->transport->set_irq_cb(vdev->transport_data);
}

/**
 * virtio_net_notify_queue - process guest doorbell kicks
 * @vdev: the base virtio device pointer
 * @queue_idx: index of the kicked virtqueue
 */
static void virtio_net_notify_queue(struct virtio_device *vdev,
				    uint16_t queue_idx)
{
	struct virtio_net_ctx *ctx = vdev->priv;
	struct modvm_net *backend = ctx->backend;
	struct virtqueue *vq;
	struct virtqueue_buf bufs[32];
	uint16_t desc_idx;
	int num_bufs;
	bool need_irq = false;

	if (unlikely(queue_idx >= vdev->nr_vqs))
		return;

	vq = vdev->vqs[queue_idx];

	/* Route to Control Plane */
	if (queue_idx == 2) {
		virtio_net_handle_ctrl(vdev, vq);
		return;
	}

	/* Ignore kicks on RX queue (VQ 0) since we process it asynchronously */
	if (queue_idx == 0)
		return;

	/* Process Data Plane (TX) */
	while ((num_bufs = virtqueue_pop(vq, &desc_idx, bufs, 32)) > 0) {
		size_t hdr_len = sizeof(struct virtio_net_hdr_v1);
		size_t offset = 0;
		uint8_t frame_buf[2048];
		size_t frame_len = 0;
		int i;

		for (i = 0; i < num_bufs; i++) {
			size_t chunk = bufs[i].len;
			uint8_t *hva = bufs[i].hva;

			if (unlikely(bufs[i].is_write))
				continue;

			if (offset < hdr_len) {
				size_t skip = (hdr_len - offset < chunk) ?
						      (hdr_len - offset) :
						      chunk;
				offset += skip;
				chunk -= skip;
				hva += skip;
			}

			if (chunk > 0) {
				if (likely(frame_len + chunk <=
					   sizeof(frame_buf))) {
					memcpy(frame_buf + frame_len, hva,
					       chunk);
					frame_len += chunk;
				}
			}
		}

		if (likely(frame_len > 0 && backend && backend->ops->write))
			backend->ops->write(backend, frame_buf, frame_len);

		virtqueue_push(vq, desc_idx, 0);
		need_irq = true;
	}

	if (likely(need_irq && vdev->transport && vdev->transport->set_irq_cb))
		vdev->transport->set_irq_cb(vdev->transport_data);
}

/**
 * virtio_net_rx_cb - process incoming network frames (RX)
 * @data: closure payload pointing to the virtio_net_ctx
 * @buf: raw ethernet frame delivered by the host
 * @len: ethernet frame length
 *
 * Called asynchronously by the event loop. Fetches a writable buffer from VQ 0,
 * prepends the mandatory Virtio header, copies the frame, and notifies the guest.
 */
static void virtio_net_rx_cb(void *data, const uint8_t *buf, size_t len)
{
	struct virtio_net_ctx *ctx = data;
	struct virtio_device *vdev = &ctx->vdev;
	struct virtqueue *vq = vdev->vqs[0];
	struct virtqueue_buf bufs[32];
	uint16_t desc_idx;
	int num_bufs;
	struct virtio_net_hdr_v1 hdr;
	size_t hdr_len = sizeof(hdr);
	size_t offset = 0;
	size_t written = 0;
	int i;

	memset(&hdr, 0, hdr_len);

	/*
	 * [CRITICAL] Virtio 1.0 Spec requirement:
	 * If VIRTIO_NET_F_MRG_RXBUF is NOT negotiated, num_buffers MUST be set to 1.
	 * Using strict little-endian assignment ensures cross-arch compliance.
	 */
	hdr.num_buffers = cpu_to_le16(1);

	num_bufs = virtqueue_pop(vq, &desc_idx, bufs, 32);
	if (unlikely(num_bufs <= 0))
		return;

	for (i = 0; i < num_bufs; i++) {
		size_t chunk = bufs[i].len;
		uint8_t *hva = bufs[i].hva;

		if (unlikely(!bufs[i].is_write))
			continue;

		if (offset < hdr_len) {
			size_t cpy = (hdr_len - offset < chunk) ?
					     (hdr_len - offset) :
					     chunk;
			memcpy(hva, (uint8_t *)&hdr + offset, cpy);
			offset += cpy;
			chunk -= cpy;
			hva += cpy;
			written += cpy;
		}

		if (chunk > 0 && offset >= hdr_len) {
			size_t payload_offset = offset - hdr_len;
			if (likely(payload_offset < len)) {
				size_t cpy = (len - payload_offset < chunk) ?
						     (len - payload_offset) :
						     chunk;
				memcpy(hva, buf + payload_offset, cpy);
				offset += cpy;
				written += cpy;
			}
		}
	}

	virtqueue_push(vq, desc_idx, (uint32_t)written);

	if (likely(vdev->transport && vdev->transport->set_irq_cb))
		vdev->transport->set_irq_cb(vdev->transport_data);
}

static const struct virtio_device_ops virtio_net_ops = {
	.realize = virtio_net_realize,
	.unrealize = virtio_net_unrealize,
	.get_features = virtio_net_get_features,
	.read_config = virtio_net_read_config,
	.notify_queue = virtio_net_notify_queue,
};

/**
 * virtio_net_create - allocate a virtio network front-end device
 * @ctx: global virtual machine context
 * @backend: host network abstraction to pair with
 *
 * Return: initialized virtio device pointer ready for transport mounting,
 * or NULL on allocation failure.
 */
struct virtio_device *virtio_net_create(struct modvm_ctx *ctx,
					struct modvm_net *backend)
{
	struct virtio_net_ctx *net_ctx;

	if (WARN_ON(!ctx || !backend))
		return NULL;

	net_ctx = calloc(1, sizeof(*net_ctx));
	if (!net_ctx)
		return NULL;

	net_ctx->backend = backend;
	net_ctx->vdev.device_id = VIRTIO_ID_NET;
	net_ctx->vdev.ops = &virtio_net_ops;
	net_ctx->vdev.priv = net_ctx;

	return &net_ctx->vdev;
}