/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_VIRTIO_VIRTIO_H
#define MODVM_HW_VIRTIO_VIRTIO_H

#include <stdint.h>
#include <modvm/core/device.h>
#include <modvm/core/irq.h>
#include <modvm/hw/virtio/virtqueue.h>

#define VIRTIO_MMIO_MAGIC 0x74726976 /* "virt" */
#define VIRTIO_MMIO_VERSION_1 2 /* Virtio 1.0 (v2) */
#define VIRTIO_VENDOR_ID 0x554D4551 /* "QEMU" */

#define VIRTIO_MAX_VQS 8

struct virtio_device;

/**
 * struct virtio_device_ops - operations for specific virtio backends (e.g., blk, net)
 * @realize: initialize backend-specific state
 * @unrealize: teardown backend-specific state
 * @reset: clear backend state upon guest reset request
 * @get_features: retrieve the feature bitmask supported by the backend
 * @set_features: acknowledge the features negotiated by the guest
 * @read_config: read from the device-specific configuration space
 * @write_config: write to the device-specific configuration space
 * @notify_queue: handle a guest kick (queue doorbell) for a specific virtqueue
 */
struct virtio_device_ops {
	int (*realize)(struct virtio_device *vdev);
	void (*unrealize)(struct virtio_device *vdev);
	void (*reset)(struct virtio_device *vdev);
	uint64_t (*get_features)(struct virtio_device *vdev);
	void (*set_features)(struct virtio_device *vdev, uint64_t features);
	uint32_t (*read_config)(struct virtio_device *vdev, uint64_t offset,
				uint8_t size);
	void (*write_config)(struct virtio_device *vdev, uint64_t offset,
			     uint32_t val, uint8_t size);
	void (*notify_queue)(struct virtio_device *vdev, uint16_t queue_idx);
};

/**
 * struct virtio_device - abstract base class for all virtio devices
 * @parent_dev: the underlying transport device (e.g., MMIO device)
 * @ops: the backend-specific operations table
 * @priv: opaque pointer to the backend state (e.g., block device state)
 * @device_id: standard Virtio subsystem identifier (e.g., 2 for Block)
 * @vqs: array of managed virtqueues
 * @nr_vqs: number of active virtqueues
 */
struct virtio_device {
	struct modvm_device *parent_dev;
	const struct virtio_device_ops *ops;
	void *priv;
	uint32_t device_id;

	struct virtqueue *vqs[VIRTIO_MAX_VQS];
	uint16_t nr_vqs;
};

/**
 * struct virtio_mmio_pdata - platform routing data for a Virtio-MMIO transport
 * @base: the starting address on the MMIO bus
 * @irq: the pre-wired interrupt line to signal the processor
 * @vdev: the specific virtio backend payload to wrap
 */
struct virtio_mmio_pdata {
	uint64_t base;
	struct modvm_irq *irq;
	struct virtio_device *vdev;
};

void virtio_mmio_set_irq(struct virtio_device *vdev);

#endif /* MODVM_HW_VIRTIO_VIRTIO_H */