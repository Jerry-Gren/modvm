/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <modvm/core/bus.h>
#include <modvm/core/devm.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/utils/compiler.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>

#include "virtqueue.h"
#include "virtio_mmio_reg.h"

#undef pr_fmt
#define pr_fmt(fmt) "virtio_mmio: " fmt

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FAILED 128

struct virtio_mmio_ctx {
	struct virtio_device *vdev;
	struct modvm_irq *irq;

	uint32_t status;
	uint32_t device_features_sel;
	uint32_t driver_features_sel;
	uint64_t driver_features;
	uint32_t queue_sel;
	uint32_t interrupt_status;

	/* Temporary state for Virtio 1.0 64-bit address assembly */
	uint32_t queue_desc_lo[VIRTIO_MAX_VQS];
	uint32_t queue_desc_hi[VIRTIO_MAX_VQS];
	uint32_t queue_avail_lo[VIRTIO_MAX_VQS];
	uint32_t queue_avail_hi[VIRTIO_MAX_VQS];
	uint32_t queue_used_lo[VIRTIO_MAX_VQS];
	uint32_t queue_used_hi[VIRTIO_MAX_VQS];
	uint32_t queue_num[VIRTIO_MAX_VQS];
	bool queue_ready[VIRTIO_MAX_VQS];
};

static void virtio_mmio_transport_set_irq_cb(void *transport_data)
{
	struct virtio_mmio_ctx *ctx = transport_data;

	ctx->interrupt_status |= VIRTIO_MMIO_INT_VRING;
	modvm_irq_set_level(ctx->irq, 1);
}

static const struct virtio_transport_ops virtio_mmio_transport = {
	.set_irq_cb = virtio_mmio_transport_set_irq_cb,
};

static uint64_t virtio_mmio_read(struct modvm_device *dev, uint64_t offset,
				 uint8_t size)
{
	struct virtio_mmio_ctx *ctx = dev->priv;
	struct virtio_device *vdev = ctx->vdev;
	uint64_t features;

	if (offset >= VIRTIO_MMIO_CONFIG) {
		if (likely(vdev->ops->read_config))
			return vdev->ops->read_config(
				vdev, offset - VIRTIO_MMIO_CONFIG, size);
		return 0;
	}

	switch (offset) {
	case VIRTIO_MMIO_MAGIC_VALUE:
		return VIRTIO_MMIO_MAGIC;
	case VIRTIO_MMIO_VERSION:
		return VIRTIO_MMIO_VERSION_1;
	case VIRTIO_MMIO_DEVICE_ID:
		return vdev->device_id;
	case VIRTIO_MMIO_VENDOR_ID:
		return VIRTIO_VENDOR_ID;
	case VIRTIO_MMIO_DEVICE_FEATURES:
		features = vdev->ops->get_features ?
				   vdev->ops->get_features(vdev) :
				   0;
		/* Feature bit 32 (VIRTIO_F_VERSION_1) must be advertised for v1 */
		features |= (1ULL << 32);
		if (ctx->device_features_sel == 0)
			return (uint32_t)(features & 0xFFFFFFFF);
		else if (ctx->device_features_sel == 1)
			return (uint32_t)((features >> 32) & 0xFFFFFFFF);
		return 0;
	case VIRTIO_MMIO_QUEUE_NUM_MAX:
		/* Let's fix a safe default queue depth of 128 for mmio */
		return 128;
	case VIRTIO_MMIO_QUEUE_READY:
		if (ctx->queue_sel < vdev->nr_vqs)
			return ctx->queue_ready[ctx->queue_sel] ? 1 : 0;
		return 0;
	case VIRTIO_MMIO_INTERRUPT_STATUS:
		return ctx->interrupt_status;
	case VIRTIO_MMIO_STATUS:
		return ctx->status;
	default:
		return 0;
	}
}

static void virtio_mmio_write(struct modvm_device *dev, uint64_t offset,
			      uint64_t val, uint8_t size)
{
	struct virtio_mmio_ctx *ctx = dev->priv;
	struct virtio_device *vdev = ctx->vdev;
	uint16_t q_sel = ctx->queue_sel;
	uint64_t desc_gpa, avail_gpa, used_gpa;

	if (offset >= VIRTIO_MMIO_CONFIG) {
		if (likely(vdev->ops->write_config))
			vdev->ops->write_config(
				vdev, offset - VIRTIO_MMIO_CONFIG, val, size);
		return;
	}

	switch (offset) {
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
		ctx->device_features_sel = (uint32_t)val;
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
		ctx->driver_features_sel = (uint32_t)val;
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES:
		if (ctx->driver_features_sel == 0)
			ctx->driver_features =
				(ctx->driver_features & 0xFFFFFFFF00000000ULL) |
				val;
		else if (ctx->driver_features_sel == 1)
			ctx->driver_features =
				(ctx->driver_features & 0xFFFFFFFFULL) |
				(val << 32);
		break;
	case VIRTIO_MMIO_QUEUE_SEL:
		ctx->queue_sel = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_NUM:
		if (q_sel < VIRTIO_MAX_VQS)
			ctx->queue_num[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
		if (q_sel < VIRTIO_MAX_VQS)
			ctx->queue_desc_lo[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		if (q_sel < VIRTIO_MAX_VQS)
			ctx->queue_desc_hi[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
		if (q_sel < VIRTIO_MAX_VQS)
			ctx->queue_avail_lo[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
		if (q_sel < VIRTIO_MAX_VQS)
			ctx->queue_avail_hi[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_USED_LOW:
		if (q_sel < VIRTIO_MAX_VQS)
			ctx->queue_used_lo[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_USED_HIGH:
		if (q_sel < VIRTIO_MAX_VQS)
			ctx->queue_used_hi[q_sel] = (uint32_t)val;
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		if (q_sel < vdev->nr_vqs && val == 1) {
			desc_gpa = ((uint64_t)ctx->queue_desc_hi[q_sel] << 32) |
				   ctx->queue_desc_lo[q_sel];
			avail_gpa =
				((uint64_t)ctx->queue_avail_hi[q_sel] << 32) |
				ctx->queue_avail_lo[q_sel];
			used_gpa = ((uint64_t)ctx->queue_used_hi[q_sel] << 32) |
				   ctx->queue_used_lo[q_sel];

			if (virtqueue_set_addrs(vdev->vqs[q_sel], desc_gpa,
						avail_gpa, used_gpa) == 0)
				ctx->queue_ready[q_sel] = true;
		}
		break;
	case VIRTIO_MMIO_QUEUE_NOTIFY:
		if (val < vdev->nr_vqs && likely(vdev->ops->notify_queue))
			vdev->ops->notify_queue(vdev, (uint16_t)val);
		break;
	case VIRTIO_MMIO_INTERRUPT_ACK:
		ctx->interrupt_status &= ~val;
		if (ctx->interrupt_status == 0)
			modvm_irq_set_level(ctx->irq, 0);
		break;
	case VIRTIO_MMIO_STATUS:
		ctx->status = (uint32_t)val;
		if (ctx->status == 0) {
			if (vdev->ops->reset)
				vdev->ops->reset(vdev);
		} else if (ctx->status & VIRTIO_STATUS_FEATURES_OK) {
			if (vdev->ops->set_features)
				vdev->ops->set_features(vdev,
							ctx->driver_features);
		}
		break;
	default:
		break;
	}
}

static const struct modvm_device_ops virtio_mmio_ops = {
	.read = virtio_mmio_read,
	.write = virtio_mmio_write,
};

static int virtio_mmio_instantiate(struct modvm_device *dev, void *pdata)
{
	struct virtio_mmio_pdata *plat = pdata;
	struct virtio_mmio_ctx *ctx;
	struct virtio_device *vdev;
	int ret;

	if (WARN_ON(!plat || !plat->vdev || !plat->irq))
		return -EINVAL;

	ctx = modvm_devm_zalloc(dev, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	vdev = plat->vdev;
	vdev->parent_dev = dev;

	vdev->transport = &virtio_mmio_transport;
	vdev->transport_data = ctx;
	vdev->mem = plat->mem_space;

	ctx->vdev = vdev;
	ctx->irq = plat->irq;

	dev->ops = &virtio_mmio_ops;
	dev->priv = ctx;

	/* Backend lifecycle initialization */
	if (vdev->ops->realize) {
		ret = vdev->ops->realize(vdev);
		if (ret < 0)
			return ret;
	}

	/* 0x200 bytes maps standard registers and 0x100 for config space */
	ret = modvm_bus_register_region(MODVM_BUS_MMIO, plat->base, 0x200, dev);
	if (ret < 0)
		return ret;

	pr_info("virtio-mmio transport mapped at 0x%08lx for device %u\n",
		plat->base, vdev->device_id);
	return 0;
}

static const struct modvm_device_class virtio_mmio_class = {
	.name = "virtio-mmio",
	.instantiate = virtio_mmio_instantiate,
};

static void __attribute__((constructor)) register_virtio_mmio_class(void)
{
	modvm_device_class_register(&virtio_mmio_class);
}