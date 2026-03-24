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
	 */
	vdev->vqs[0] =
		virtqueue_create(&mctx->accel.mem_space, VIRTIO_NET_QUEUE_SIZE);
	vdev->vqs[1] =
		virtqueue_create(&mctx->accel.mem_space, VIRTIO_NET_QUEUE_SIZE);

	if (!vdev->vqs[0] || !vdev->vqs[1])
		return -ENOMEM;

	vdev->nr_vqs = 2;

	memset(&ctx->config, 0, sizeof(ctx->config));
	if (ctx->backend && ctx->backend->ops->get_mac)
		ctx->backend->ops->get_mac(ctx->backend, ctx->config.mac);

	ctx->config.status = (virtio16_t)cpu_to_le16(VIRTIO_NET_S_LINK_UP);

	/* Bind the asynchronous host reception event to the Virtio-Net context */
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
	return (1ULL << VIRTIO_NET_F_MAC) | (1ULL << VIRTIO_NET_F_STATUS);
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
 * virtio_net_notify_queue - process outgoing network frames (TX)
 * @vdev: the base virtio device pointer
 * @queue_idx: index of the kicked virtqueue
 *
 * Scavenges the transmit queue for guest-provided ethernet frames, strips the
 * Virtio header, and injects the raw payload into the host network stack.
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

	/* TX operations are exclusively routed to VQ 1 */
	if (unlikely(queue_idx != 1))
		return;

	vq = vdev->vqs[queue_idx];

	while ((num_bufs = virtqueue_pop(vq, &desc_idx, bufs, 32)) > 0) {
		size_t hdr_len = sizeof(struct virtio_net_hdr_v1);
		size_t offset = 0;
		uint8_t frame_buf[2048];
		size_t frame_len = 0;
		int i;

		for (i = 0; i < num_bufs; i++) {
			size_t chunk = bufs[i].len;
			uint8_t *hva = bufs[i].hva;

			/* TX buffers must be read-only from the device perspective */
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