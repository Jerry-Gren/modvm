/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_VIRTIO_VIRTIO_H
#define MODVM_HW_VIRTIO_VIRTIO_H

#include <stdint.h>
#include <modvm/core/device.h>
#include <modvm/core/irq.h>

#define VIRTIO_MMIO_MAGIC 0x74726976 /* "virt" */
#define VIRTIO_MMIO_VERSION_1 2 /* Virtio 1.0 (v2) */
#define VIRTIO_VENDOR_ID 0x554D4551 /* "QEMU" */

#define VIRTIO_MAX_VQS 8

struct virtio_device;
struct virtqueue;

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
	uint64_t (*read_config)(struct virtio_device *vdev, uint64_t offset,
				uint8_t size);
	void (*write_config)(struct virtio_device *vdev, uint64_t offset,
			     uint32_t val, uint8_t size);
	void (*notify_queue)(struct virtio_device *vdev, uint16_t queue_idx);
};

/**
 * struct virtio_transport_ops - methods provided by the parent transport bus
 * @set_irq_cb: inject an interrupt into the guest operating system
 */
struct virtio_transport_ops {
	void (*set_irq_cb)(void *transport_data);
};

/**
 * struct virtio_device - abstract base class for all virtio devices
 * @parent_dev: the underlying transport device (e.g., MMIO device)
 * @transport: dispatch table to the parent bus
 * @transport_data: opaque closure representing the parent bus context
 * @ops: the backend-specific operations table
 * @priv: opaque pointer to the backend state (e.g., block device state)
 * @device_id: standard Virtio subsystem identifier (e.g., 2 for Block)
 * @vqs: array of managed virtqueues
 * @nr_vqs: number of active virtqueues
 */
struct virtio_device {
	struct modvm_device *parent_dev;

	const struct virtio_transport_ops *transport;
	void *transport_data;
	struct modvm_mem_space *mem;

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
 * @mem_space: ?
 */
struct virtio_mmio_pdata {
	uint64_t base;
	struct modvm_irq *irq;
	struct virtio_device *vdev;
	struct modvm_mem_space *mem_space;
};

#endif /* MODVM_HW_VIRTIO_VIRTIO_H */