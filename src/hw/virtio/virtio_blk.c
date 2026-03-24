/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/block.h>
#include <modvm/core/modvm.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_blk.h>
#include <modvm/utils/byteorder.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

#include "virtqueue.h"
#include "virtio_blk_reg.h"

#undef pr_fmt
#define pr_fmt(fmt) "virtio_blk: " fmt

#define VIRTIO_ID_BLOCK 2
#define VIRTIO_BLK_QUEUE_SIZE 128
#define SECTOR_SIZE 512

/**
 * struct virtio_blk_ctx - state container for a virtio block instance
 * @vdev: the exposed virtio device abstraction
 * @backend: host storage backend performing actual data manipulation
 * @config: cached standard configuration space
 */
struct virtio_blk_ctx {
	struct virtio_device vdev;
	struct modvm_block *backend;
	struct virtio_blk_config config;
};

/**
 * virtio_blk_realize - initialize internal device state and allocate virtqueues
 * @vdev: the base virtio device pointer
 *
 * Return: 0 on success, or a negative error code.
 */
static int virtio_blk_realize(struct virtio_device *vdev)
{
	struct virtio_blk_ctx *ctx = vdev->priv;
	struct modvm_ctx *mctx = vdev->parent_dev->ctx;
	uint64_t capacity_bytes;

	/* Request the generic virtio-mmio layer to allocate 1 virtqueue */
	vdev->vqs[0] =
		virtqueue_create(&mctx->accel.mem_space, VIRTIO_BLK_QUEUE_SIZE);
	if (!vdev->vqs[0])
		return -ENOMEM;

	vdev->nr_vqs = 1;

	/* Populate standard block geometry and capacity into config space */
	capacity_bytes = ctx->backend->ops->get_capacity(ctx->backend);

	memset(&ctx->config, 0, sizeof(ctx->config));
	ctx->config.capacity = cpu_to_le64(capacity_bytes / SECTOR_SIZE);
	ctx->config.blk_size = cpu_to_le32(SECTOR_SIZE);
	ctx->config.size_max = cpu_to_le32(65536);
	ctx->config.seg_max = cpu_to_le32(
		128 - 2); /* Account for header and status descriptors */

	/* Synthetic standard geometry */
	ctx->config.geometry.cylinders = cpu_to_le16(
		(uint16_t)((capacity_bytes / SECTOR_SIZE) / (16 * 63)));
	ctx->config.geometry.heads = 16;
	ctx->config.geometry.sectors = 63;

	pr_info("virtio-blk realized with capacity %llu sectors\n",
		(unsigned long long)(capacity_bytes / SECTOR_SIZE));

	return 0;
}

/**
 * virtio_blk_unrealize - cleanly teardown local allocations
 * @vdev: the base virtio device pointer
 *
 * Provides the lifecycle counterpart to standard allocation.
 */
static void virtio_blk_unrealize(struct virtio_device *vdev)
{
	struct virtio_blk_ctx *ctx = vdev->priv;
	int i;

	if (WARN_ON(!ctx))
		return;

	for (i = 0; i < vdev->nr_vqs; i++) {
		if (vdev->vqs[i])
			virtqueue_destroy(vdev->vqs[i]);
	}

	free(ctx);
}

static uint64_t virtio_blk_get_features(struct virtio_device *vdev)
{
	(void)vdev;
	/* We expose minimal modern features for stable block I/O */
	return VIRTIO_BLK_F_BLK_SIZE | VIRTIO_BLK_F_GEOMETRY |
	       VIRTIO_BLK_F_FLUSH;
}

static uint64_t virtio_blk_read_config(struct virtio_device *vdev,
				       uint64_t offset, uint8_t size)
{
	struct virtio_blk_ctx *ctx = vdev->priv;
	uint64_t val = 0;

	if (WARN_ON(offset + size > sizeof(ctx->config)))
		return 0;

	memcpy(&val, (uint8_t *)&ctx->config + offset, size);
	return val;
}

/**
 * virtio_blk_notify_queue - process I/O requests submitted by the guest
 * @vdev: the base virtio device pointer
 * @queue_idx: index of the virtqueue that received a doorbell kick
 *
 * This acts as the primary data plane (Hot Path). It pops descriptor chains,
 * validates layout, performs host I/O, pushes status, and injects interrupts.
 */
static void virtio_blk_notify_queue(struct virtio_device *vdev,
				    uint16_t queue_idx)
{
	struct virtio_blk_ctx *ctx = vdev->priv;
	struct modvm_block *backend = ctx->backend;
	struct virtqueue *vq;
	struct virtqueue_buf bufs[VIRTIO_BLK_QUEUE_SIZE];
	uint16_t desc_idx;
	int num_bufs;
	bool need_irq = false;

	if (unlikely(queue_idx >= vdev->nr_vqs))
		return;

	vq = vdev->vqs[queue_idx];

	while ((num_bufs = virtqueue_pop(vq, &desc_idx, bufs,
					 VIRTIO_BLK_QUEUE_SIZE)) > 0) {
		struct virtio_blk_outhdr *hdr;
		uint8_t *status_byte;
		uint64_t offset;
		uint32_t written_len = 0;
		uint32_t type;
		int i;
		ssize_t io_ret;

		/* Minimal valid chain: Header -> [Data...] -> Status */
		if (unlikely(num_bufs < 2)) {
			pr_err("virtio-blk dropped malformed descriptor chain\n");
			continue;
		}

		hdr = bufs[0].hva;
		status_byte = bufs[num_bufs - 1].hva;

		if (unlikely(bufs[0].len < sizeof(*hdr) ||
			     !bufs[num_bufs - 1].is_write)) {
			*status_byte = VIRTIO_BLK_S_IOERR;
			goto push_desc;
		}

		type = le32_to_cpu(hdr->type);
		offset = le64_to_cpu(hdr->sector) * SECTOR_SIZE;

		switch (type) {
		case VIRTIO_BLK_T_IN:
			*status_byte = VIRTIO_BLK_S_OK;
			for (i = 1; i < num_bufs - 1; i++) {
				if (likely(bufs[i].is_write)) {
					io_ret = backend->ops->read(backend,
								    bufs[i].hva,
								    bufs[i].len,
								    offset);
					if (unlikely(io_ret !=
						     (ssize_t)bufs[i].len)) {
						*status_byte =
							VIRTIO_BLK_S_IOERR;
						break;
					}
					offset += bufs[i].len;
					written_len += bufs[i].len;
				}
			}
			break;

		case VIRTIO_BLK_T_OUT:
			*status_byte = VIRTIO_BLK_S_OK;
			for (i = 1; i < num_bufs - 1; i++) {
				if (likely(!bufs[i].is_write)) {
					io_ret = backend->ops->write(
						backend, bufs[i].hva,
						bufs[i].len, offset);
					if (unlikely(io_ret !=
						     (ssize_t)bufs[i].len)) {
						*status_byte =
							VIRTIO_BLK_S_IOERR;
						break;
					}
					offset += bufs[i].len;
				}
			}
			break;

		case VIRTIO_BLK_T_FLUSH:
			*status_byte = VIRTIO_BLK_S_OK;
			break;

		case VIRTIO_BLK_T_GET_ID:
			/* Synthesize a generic hardcoded serial number for simplicity */
			if (bufs[1].is_write && bufs[1].len >= 10) {
				strncpy(bufs[1].hva, "MODVM-DISK", bufs[1].len);
				written_len += 10;
				*status_byte = VIRTIO_BLK_S_OK;
			} else {
				*status_byte = VIRTIO_BLK_S_UNSUPP;
			}
			break;

		default:
			*status_byte = VIRTIO_BLK_S_UNSUPP;
			break;
		}

push_desc:
		/* +1 to account for the status byte appended by the device */
		virtqueue_push(vq, desc_idx, written_len + 1);
		need_irq = true;
	}

	if (likely(need_irq && vdev->transport && vdev->transport->set_irq_cb))
		vdev->transport->set_irq_cb(vdev->transport_data);
}

static const struct virtio_device_ops virtio_blk_ops = {
	.realize = virtio_blk_realize,
	.unrealize = virtio_blk_unrealize,
	.get_features = virtio_blk_get_features,
	.read_config = virtio_blk_read_config,
	.notify_queue = virtio_blk_notify_queue,
};

/**
 * virtio_blk_create - allocate a virtio block front-end device
 * @ctx: global virtual machine context
 * @backend: host block storage abstraction to pair with
 *
 * Return: initialized virtio device pointer ready for transport mounting,
 * or NULL on allocation failure.
 */
struct virtio_device *virtio_blk_create(struct modvm_ctx *ctx,
					struct modvm_block *backend)
{
	struct virtio_blk_ctx *blk_ctx;

	if (WARN_ON(!ctx || !backend))
		return NULL;

	blk_ctx = calloc(1, sizeof(*blk_ctx));
	if (!blk_ctx)
		return NULL;

	blk_ctx->backend = backend;
	blk_ctx->vdev.device_id = VIRTIO_ID_BLOCK;
	blk_ctx->vdev.ops = &virtio_blk_ops;
	blk_ctx->vdev.priv = blk_ctx;

	return &blk_ctx->vdev;
}