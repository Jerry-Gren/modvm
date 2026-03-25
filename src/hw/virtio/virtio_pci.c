/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/bus.h>
#include <modvm/core/devm.h>
#include <modvm/core/pci.h>
#include <modvm/hw/virtio/virtio.h>
#include <modvm/hw/virtio/virtio_pci.h>
#include <modvm/os/page.h>
#include <modvm/utils/byteorder.h>
#include <modvm/utils/compiler.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>

#include "virtqueue.h"
#include "virtio_pci_reg.h"

#undef pr_fmt
#define pr_fmt(fmt) "virtio_pci: " fmt

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FAILED 128

/**
 * struct virtio_pci_ctx - state container for the Virtio-PCI transport layer
 * @pci_dev: standard PCI endpoint structure
 * @vdev: frontend virtio device payload
 * @common_cfg: cached common configuration block mapped to BAR0
 * @driver_features: full 64-bit feature mask negotiated by guest
 * @isr_status: pending interrupt status
 * @bar0_size: dynamically cached host page size
 * @queue_ready: tracking array for virtqueue activation
 */
struct virtio_pci_ctx {
	struct modvm_pci_device pci_dev;
	struct virtio_device *vdev;

	struct virtio_pci_common_cfg common_cfg;
	uint64_t driver_features;
	uint8_t isr_status;
	uint32_t bar0_size;
	bool queue_ready[VIRTIO_MAX_VQS];
};

static void virtio_pci_transport_set_irq_cb(void *transport_data)
{
	struct virtio_pci_ctx *ctx = transport_data;

	ctx->isr_status |= 1;
	modvm_pci_device_set_irq(&ctx->pci_dev, 1);
}

static const struct virtio_transport_ops virtio_pci_transport = {
	.set_irq_cb = virtio_pci_transport_set_irq_cb,
};

static uint32_t virtio_pci_read_config(struct modvm_pci_device *pci_dev,
				       uint8_t offset, uint8_t size)
{
	if (unlikely(offset + size > PCI_CONFIG_SPACE_SIZE))
		return ~0U;

	switch (size) {
	case 1:
		return pci_dev->config_space[offset];
	case 2: {
		le16_t val;
		memcpy(&val, &pci_dev->config_space[offset], 2);
		return le16_to_cpu(val);
	}
	case 4: {
		le32_t val;
		memcpy(&val, &pci_dev->config_space[offset], 4);
		return le32_to_cpu(val);
	}
	default:
		return ~0U;
	}
}

static void virtio_pci_write_config(struct modvm_pci_device *pci_dev,
				    uint8_t offset, uint32_t val, uint8_t size)
{
	struct virtio_pci_ctx *ctx = pci_dev->priv;

	if (unlikely(offset + size > PCI_CONFIG_SPACE_SIZE))
		return;

	if (offset == 0x10 && val == 0xFFFFFFFF && size == 4) {
		le32_t size_mask = cpu_to_le32(~(ctx->bar0_size - 1));
		memcpy(&pci_dev->config_space[offset], &size_mask, 4);
		return;
	}

	switch (size) {
	case 1:
		pci_dev->config_space[offset] = (uint8_t)val;
		break;
	case 2: {
		le16_t v16 = cpu_to_le16((uint16_t)val);
		memcpy(&pci_dev->config_space[offset], &v16, 2);
		break;
	}
	case 4: {
		le32_t v32 = cpu_to_le32(val);
		memcpy(&pci_dev->config_space[offset], &v32, 4);
		break;
	}
	}
}

static uint64_t virtio_pci_bar0_read(struct modvm_device *dev, uint64_t offset,
				     uint8_t size)
{
	struct virtio_pci_ctx *ctx = dev->priv;
	struct virtio_device *vdev = ctx->vdev;
	uint32_t val = 0;
	uint64_t features;

	if (offset < sizeof(struct virtio_pci_common_cfg)) {
		if (offset ==
		    offsetof(struct virtio_pci_common_cfg, device_feature)) {
			features = vdev->ops->get_features ?
					   vdev->ops->get_features(vdev) :
					   0;
			features |=
				(1ULL << 32); /* Advertise VIRTIO_F_VERSION_1 */

			if (le32_to_cpu(
				    ctx->common_cfg.device_feature_select) == 0)
				val = (uint32_t)(features & 0xFFFFFFFF);
			else if (le32_to_cpu(ctx->common_cfg
						     .device_feature_select) ==
				 1)
				val = (uint32_t)((features >> 32) & 0xFFFFFFFF);

			return val; /* Guest expects native byte order for MMIO read return value in our bus */
		}

		if (offset ==
		    offsetof(struct virtio_pci_common_cfg, queue_enable)) {
			uint16_t q_sel =
				le16_to_cpu(ctx->common_cfg.queue_select);
			if (q_sel < vdev->nr_vqs)
				return ctx->queue_ready[q_sel] ? 1 : 0;
			return 0;
		}

		switch (size) {
		case 1:
			val = *((uint8_t *)&ctx->common_cfg + offset);
			break;
		case 2: {
			le16_t v16;
			memcpy(&v16, (uint8_t *)&ctx->common_cfg + offset, 2);
			val = le16_to_cpu(v16);
			break;
		}
		case 4: {
			le32_t v32;
			memcpy(&v32, (uint8_t *)&ctx->common_cfg + offset, 4);
			val = le32_to_cpu(v32);
			break;
		}
		}
		return val;
	}

	if (offset == 0x200) { /* ISR Config Region */
		val = ctx->isr_status;
		ctx->isr_status = 0;
		modvm_pci_device_set_irq(&ctx->pci_dev, 0); /* Acknowledge */
		return val;
	}

	if (offset >= 0x300) { /* Device Config Region */
		if (vdev->ops->read_config)
			return vdev->ops->read_config(vdev, offset - 0x300,
						      size);
	}

	return 0;
}

static void virtio_pci_bar0_write(struct modvm_device *dev, uint64_t offset,
				  uint64_t val, uint8_t size)
{
	struct virtio_pci_ctx *ctx = dev->priv;
	struct virtio_device *vdev = ctx->vdev;
	uint16_t q_sel;

	if (offset < sizeof(struct virtio_pci_common_cfg)) {
		switch (size) {
		case 1:
			*((uint8_t *)&ctx->common_cfg + offset) = (uint8_t)val;
			break;
		case 2: {
			le16_t v16 = cpu_to_le16((uint16_t)val);
			memcpy((uint8_t *)&ctx->common_cfg + offset, &v16, 2);
			break;
		}
		case 4: {
			le32_t v32 = cpu_to_le32((uint32_t)val);
			memcpy((uint8_t *)&ctx->common_cfg + offset, &v32, 4);
			break;
		}
		}

		q_sel = le16_to_cpu(ctx->common_cfg.queue_select);

		if (offset ==
		    offsetof(struct virtio_pci_common_cfg, queue_select)) {
			if (q_sel < vdev->nr_vqs)
				ctx->common_cfg.queue_size = cpu_to_le16(
					virtqueue_get_size(vdev->vqs[q_sel]));
			else
				ctx->common_cfg.queue_size = cpu_to_le16(0);
			return;
		}

		if (offset ==
		    offsetof(struct virtio_pci_common_cfg, guest_feature)) {
			uint32_t g_val =
				le32_to_cpu(ctx->common_cfg.guest_feature);

			if (le32_to_cpu(ctx->common_cfg.guest_feature_select) ==
			    0)
				ctx->driver_features = (ctx->driver_features &
							0xFFFFFFFF00000000ULL) |
						       g_val;
			else if (le32_to_cpu(
					 ctx->common_cfg.guest_feature_select) ==
				 1)
				ctx->driver_features =
					(ctx->driver_features & 0xFFFFFFFFULL) |
					((uint64_t)g_val << 32);
			return;
		}

		if (offset == offsetof(struct virtio_pci_common_cfg,
				       queue_enable) &&
		    val == 1 && q_sel < vdev->nr_vqs) {
			uint64_t desc_gpa =
				((uint64_t)le32_to_cpu(
					 ctx->common_cfg.queue_desc_hi)
				 << 32) |
				le32_to_cpu(ctx->common_cfg.queue_desc_lo);
			uint64_t avail_gpa =
				((uint64_t)le32_to_cpu(
					 ctx->common_cfg.queue_avail_hi)
				 << 32) |
				le32_to_cpu(ctx->common_cfg.queue_avail_lo);
			uint64_t used_gpa =
				((uint64_t)le32_to_cpu(
					 ctx->common_cfg.queue_used_hi)
				 << 32) |
				le32_to_cpu(ctx->common_cfg.queue_used_lo);

			if (virtqueue_set_addrs(vdev->vqs[q_sel], desc_gpa,
						avail_gpa, used_gpa) == 0)
				ctx->queue_ready[q_sel] = true;
			return;
		}

		if (offset ==
		    offsetof(struct virtio_pci_common_cfg, device_status)) {
			if (ctx->common_cfg.device_status == 0) {
				if (vdev->ops->reset)
					vdev->ops->reset(vdev);
			} else if (ctx->common_cfg.device_status &
				   VIRTIO_STATUS_FEATURES_OK) {
				if (vdev->ops->set_features)
					vdev->ops->set_features(
						vdev, ctx->driver_features);
			}
		}
		return;
	}

	if (offset == 0x100) { /* Notify Config Region */
		q_sel = (uint16_t)val;
		if (q_sel < vdev->nr_vqs && likely(vdev->ops->notify_queue))
			vdev->ops->notify_queue(vdev, q_sel);
		return;
	}

	if (offset >= 0x300) { /* Device Config Region */
		if (vdev->ops->write_config)
			vdev->ops->write_config(vdev, offset - 0x300,
						(uint32_t)val, size);
	}
}

static const struct modvm_device_ops virtio_pci_bar_ops = {
	.read = virtio_pci_bar0_read,
	.write = virtio_pci_bar0_write,
};

static const struct modvm_pci_device_ops virtio_pci_config_ops = {
	.read_config = virtio_pci_read_config,
	.write_config = virtio_pci_write_config,
};

/**
 * virtio_pci_build_config_space - construct the standard PCI capability list
 * @ctx: the virtio pci context
 * @bar0_base: hardware assigned MMIO base address
 */
static void virtio_pci_build_config_space(struct virtio_pci_ctx *ctx,
					  uint64_t bar0_base)
{
	uint8_t *cfg = ctx->pci_dev.config_space;
	le16_t v16;
	le32_t v32;
	struct virtio_pci_cap cap = { 0 };
	struct virtio_pci_notify_cap notif = { 0 };
	uint8_t class_code = 0x00;
	uint8_t subclass = 0x00;

	/*
	 * Map Virtio Device IDs to standard PCI Class Codes.
	 * Virtio ID 1 = Network Card, Virtio ID 2 = Block Device.
	 */
	switch (ctx->vdev->device_id) {
	case 1: /* VIRTIO_ID_NET */
		class_code = 0x02; /* Network Controller */
		subclass = 0x00; /* Ethernet Controller */
		break;
	case 2: /* VIRTIO_ID_BLOCK */
		class_code = 0x01; /* Mass Storage Controller */
		subclass = 0x80; /* Other Mass Storage */
		break;
	default:
		class_code = 0xFF; /* Unclassified Device */
		subclass = 0xFF;
		break;
	}

	/* PCI Header Type 0 */
	v16 = cpu_to_le16(0x1AF4);
	memcpy(&cfg[0x00], &v16, 2); /* Vendor: Red Hat */

	/* Virtio 1.0 Spec: Modern PCI Device ID is 0x1040 + Virtio Device ID */
	v16 = cpu_to_le16(0x1040 + ctx->vdev->device_id);
	memcpy(&cfg[0x02], &v16, 2);

	v16 = cpu_to_le16(0x0002);
	memcpy(&cfg[0x04], &v16, 2); /* Command: Memory Space Enable */

	v16 = cpu_to_le16(0x0010);
	memcpy(&cfg[0x06], &v16, 2); /* Status: Capabilities List */

	cfg[0x08] = 0x01; /* Revision ID: 1 (Virtio 1.0 Non-transitional) */
	cfg[0x09] = 0x00; /* Programming Interface */
	cfg[0x0A] = subclass; /* Subclass Code */
	cfg[0x0B] = class_code; /* Base Class Code */

	v32 = cpu_to_le32((uint32_t)bar0_base);
	memcpy(&cfg[0x10], &v32, 4); /* BAR 0 */

	/*
	 * Virtio 1.0 Spec 4.1.2.1:
	 * Non-transitional devices MUST have a PCI Subsystem Device ID matching
	 * the Virtio Device ID.
	 */
	v16 = cpu_to_le16(0x1AF4);
	memcpy(&cfg[0x2C], &v16, 2); /* Subsystem Vendor ID: Red Hat */
	v16 = cpu_to_le16((uint16_t)ctx->vdev->device_id);
	memcpy(&cfg[0x2E], &v16, 2); /* Subsystem Device ID */

	cfg[0x34] = 0x40; /* Capabilities Pointer */

	/* Capability 1: Common Configuration */
	cap.cap_vndr = 0x09; /* PCI_CAP_ID_VNDR */
	cap.cap_next = 0x50;
	cap.cap_len = sizeof(cap);
	cap.cfg_type = VIRTIO_PCI_CAP_COMMON_CFG;
	cap.bar = 0;
	cap.offset = cpu_to_le32(0x000);
	cap.length = cpu_to_le32(0x100);
	memcpy(&cfg[0x40], &cap, sizeof(cap));

	/* Capability 2: Notify Configuration */
	notif.cap.cap_vndr = 0x09;
	notif.cap.cap_next = 0x68;
	notif.cap.cap_len = sizeof(notif);
	notif.cap.cfg_type = VIRTIO_PCI_CAP_NOTIFY_CFG;
	notif.cap.bar = 0;
	notif.cap.offset = cpu_to_le32(0x100);
	notif.cap.length = cpu_to_le32(0x100);
	notif.notify_off_multiplier = cpu_to_le32(0);
	memcpy(&cfg[0x50], &notif, sizeof(notif));

	/* Capability 3: ISR Configuration */
	cap.cap_next = 0x78;
	cap.cap_len = sizeof(cap);
	cap.cfg_type = VIRTIO_PCI_CAP_ISR_CFG;
	cap.offset = cpu_to_le32(0x200);
	cap.length = cpu_to_le32(1); /* ISR is 1 byte per Virtio 1.0 Spec */
	memcpy(&cfg[0x68], &cap, sizeof(cap));

	/* Capability 4: Device Specific Configuration */
	cap.cap_next = 0x00;
	cap.cap_len = sizeof(cap);
	cap.cfg_type = VIRTIO_PCI_CAP_DEVICE_CFG;
	cap.offset = cpu_to_le32(0x300);
	cap.length = cpu_to_le32(0x100);
	memcpy(&cfg[0x78], &cap, sizeof(cap));
}

static int virtio_pci_instantiate(struct modvm_device *dev, void *pdata)
{
	struct virtio_pci_pdata *plat = pdata;
	struct virtio_pci_ctx *ctx;
	struct virtio_device *vdev;
	int ret;

	if (WARN_ON(!plat || !plat->pci_bus || !plat->vdev))
		return -EINVAL;

	ctx = modvm_devm_zalloc(dev, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	vdev = plat->vdev;
	vdev->parent_dev = dev;

	vdev->transport = &virtio_pci_transport;
	vdev->transport_data = ctx;
	vdev->mem = plat->mem_space;

	ctx->vdev = vdev;
	ctx->bar0_size = (uint32_t)os_page_size();

	if (plat->bar0_base == PCI_AUTO_MMIO) {
		plat->bar0_base =
			modvm_pci_bus_alloc_mmio(plat->pci_bus, ctx->bar0_size);
		if (!plat->bar0_base) {
			pr_err("failed to allocate mmio window for virtio-pci device\n");
			return -ENOSPC;
		}
	}

	ctx->pci_dev.parent_dev = dev;
	ctx->pci_dev.ops = &virtio_pci_config_ops;
	ctx->pci_dev.priv = ctx;
	ctx->pci_dev.devfn = plat->devfn;
	ctx->pci_dev.interrupt_pin = plat->interrupt_pin;

	dev->ops = &virtio_pci_bar_ops;
	dev->priv = ctx;

	/* Backend lifecycle initialization */
	if (vdev->ops->realize) {
		ret = vdev->ops->realize(vdev);
		if (ret < 0)
			return ret;
	}

	/* MUST set this AFTER realize() assigns vdev->nr_vqs */
	ctx->common_cfg.num_queues = cpu_to_le16(vdev->nr_vqs);

	virtio_pci_build_config_space(ctx, plat->bar0_base);

	/* Register BAR0 on the generic MMIO bus */
	ret = modvm_bus_register_region(MODVM_BUS_MMIO, plat->bar0_base,
					ctx->bar0_size, dev);
	if (ret < 0)
		return ret;

	/* Register configuration space on the abstract PCI bus */
	ret = modvm_pci_device_register(plat->pci_bus, &ctx->pci_dev);
	if (ret < 0)
		return ret;

	pr_info("virtio-pci transport attached at devfn %u for device %u\n",
		plat->devfn, vdev->device_id);
	return 0;
}

static const struct modvm_device_class virtio_pci_class = {
	.name = "virtio-pci",
	.instantiate = virtio_pci_instantiate,
};

static void __attribute__((constructor)) register_virtio_pci_class(void)
{
	modvm_device_class_register(&virtio_pci_class);
}