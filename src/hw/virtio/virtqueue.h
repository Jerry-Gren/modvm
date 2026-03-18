/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_VIRTIO_VIRTQUEUE_H
#define MODVM_HW_VIRTIO_VIRTQUEUE_H

#include <stdint.h>
#include <stdbool.h>

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

struct modvm_mem_space;
struct virtqueue;

/**
 * struct vring_desc - Virtio ring descriptor
 * @addr: guest physical address of the buffer
 * @len: length of the buffer
 * @flags: descriptor flags (e.g., next, write-only)
 * @next: index of the next descriptor in the chain
 */
struct vring_desc {
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
} __packed;

/**
 * struct virtqueue_buf - mapped buffer element for device consumption
 * @hva: host virtual address of the payload
 * @len: length of the accessible payload
 * @is_write: true if the device is permitted to write to this buffer
 */
struct virtqueue_buf {
	void *hva;
	uint32_t len;
	bool is_write;
};

struct virtqueue *virtqueue_create(struct modvm_mem_space *mem,
				   uint16_t queue_size);
void virtqueue_destroy(struct virtqueue *vq);

uint16_t virtqueue_get_size(struct virtqueue *vq);

int virtqueue_set_addrs(struct virtqueue *vq, uint64_t desc_gpa,
			uint64_t avail_gpa, uint64_t used_gpa);
int virtqueue_pop(struct virtqueue *vq, uint16_t *out_desc_idx,
		  struct virtqueue_buf *bufs, int max_bufs);
void virtqueue_push(struct virtqueue *vq, uint16_t desc_idx, uint32_t len);

#endif /* MODVM_HW_VIRTIO_VIRTQUEUE_H */