/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>
#include <stdatomic.h>

#include <modvm/core/memory.h>
#include <modvm/utils/byteorder.h>
#include <modvm/utils/compiler.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/err.h>

#include "virtqueue.h"

struct vring_avail {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[];
} __packed;

struct vring_used_elem {
	uint32_t id;
	uint32_t len;
} __packed;

struct vring_used {
	uint16_t flags;
	uint16_t idx;
	struct vring_used_elem ring[];
} __packed;

/**
 * struct virtqueue - internal representation of a virtio ring
 * @mem: reference to the virtual machine physical memory space
 * @queue_size: maximum number of descriptors
 * @last_avail_idx: host-side cached index of the next available buffer
 * @last_used_idx: host-side cached index of the next used buffer
 * @desc_table: mapped host virtual address of the descriptor table
 * @avail_ring: mapped host virtual address of the available ring
 * @used_ring: mapped host virtual address of the used ring
 */
struct virtqueue {
	struct modvm_mem_space *mem;
	uint16_t queue_size;
	uint16_t last_avail_idx;
	uint16_t last_used_idx;

	struct vring_desc *desc_table;
	struct vring_avail *avail_ring;
	struct vring_used *used_ring;
};

/**
 * virtqueue_create - allocate and initialize a virtqueue object
 * @mem: memory space for translating buffer addresses
 * @queue_size: hardware-defined maximum depth of the queue
 *
 * Return: allocated virtqueue, or NULL on failure.
 */
struct virtqueue *virtqueue_create(struct modvm_mem_space *mem,
				   uint16_t queue_size)
{
	struct virtqueue *vq;

	if (WARN_ON(!mem || queue_size == 0))
		return NULL;

	if (WARN_ON((queue_size & (queue_size - 1)) != 0))
		return NULL;

	vq = calloc(1, sizeof(*vq));
	if (!vq)
		return NULL;

	vq->mem = mem;
	vq->queue_size = queue_size;
	vq->last_avail_idx = 0;
	vq->last_used_idx = 0;

	return vq;
}

/**
 * virtqueue_get_size - retrieve the configured maximum depth of the queue
 * @vq: the virtqueue instance
 *
 * Return: number of descriptors the queue can hold, or 0 if invalid.
 */
uint16_t virtqueue_get_size(struct virtqueue *vq)
{
	if (WARN_ON(!vq))
		return 0;

	return vq->queue_size;
}

/**
 * virtqueue_set_addrs - bind guest physical rings to the host structures
 * @vq: the virtqueue instance
 * @desc_gpa: guest physical address of the descriptor table
 * @avail_gpa: guest physical address of the available ring
 * @used_gpa: guest physical address of the used ring
 *
 * Return: 0 on success, or a negative error code.
 */
int virtqueue_set_addrs(struct virtqueue *vq, uint64_t desc_gpa,
			uint64_t avail_gpa, uint64_t used_gpa)
{
	if (WARN_ON(!vq))
		return -EINVAL;

	vq->desc_table = modvm_mem_gpa_to_hva(vq->mem, desc_gpa);
	vq->avail_ring = modvm_mem_gpa_to_hva(vq->mem, avail_gpa);
	vq->used_ring = modvm_mem_gpa_to_hva(vq->mem, used_gpa);

	if (!vq->desc_table || !vq->avail_ring || !vq->used_ring)
		return -EFAULT;

	return 0;
}

/**
 * virtqueue_pop - fetch the next pending buffer chain from the guest
 * @vq: the virtqueue instance
 * @out_desc_idx: pointer to accept the head descriptor index
 * @bufs: array to populate with translated host pointers
 * @max_bufs: maximum capacity of the provided array
 *
 * This function resides on the hot path and processes I/O rings dynamically.
 *
 * Return: number of buffers populated, 0 if empty, or a negative error code.
 */
int virtqueue_pop(struct virtqueue *vq, uint16_t *out_desc_idx,
		  struct virtqueue_buf *bufs, int max_bufs)
{
	uint16_t avail_idx;
	uint16_t head_idx;
	uint16_t current_idx;
	int num_bufs = 0;

	if (unlikely(!vq || !vq->avail_ring || !vq->desc_table))
		return -EINVAL;

	avail_idx = le16_to_cpu(vq->avail_ring->idx);
	atomic_thread_fence(memory_order_acquire);

	if (unlikely(vq->last_avail_idx == avail_idx))
		return 0;

	head_idx = le16_to_cpu(
		vq->avail_ring->ring[vq->last_avail_idx % vq->queue_size]);

	if (unlikely(head_idx >= vq->queue_size))
		return -EFAULT;

	*out_desc_idx = head_idx;
	current_idx = head_idx;

	do {
		struct vring_desc *desc = &vq->desc_table[current_idx];

		if (unlikely(num_bufs >= max_bufs))
			return -ENOSPC;

		bufs[num_bufs].hva =
			modvm_mem_gpa_to_hva(vq->mem, le64_to_cpu(desc->addr));
		if (unlikely(!bufs[num_bufs].hva))
			return -EFAULT;

		bufs[num_bufs].len = le32_to_cpu(desc->len);
		bufs[num_bufs].is_write =
			(le16_to_cpu(desc->flags) & VRING_DESC_F_WRITE) != 0;
		num_bufs++;

		if (!(le16_to_cpu(desc->flags) & VRING_DESC_F_NEXT))
			break;

		current_idx = le16_to_cpu(desc->next);
		if (unlikely(current_idx >= vq->queue_size))
			return -EFAULT;

	} while (true);

	vq->last_avail_idx++;
	return num_bufs;
}

/**
 * virtqueue_push - return a processed buffer chain to the guest
 * @vq: the virtqueue instance
 * @desc_idx: the head index of the completed descriptor chain
 * @len: total bytes written to the device-writable buffers
 */
void virtqueue_push(struct virtqueue *vq, uint16_t desc_idx, uint32_t len)
{
	struct vring_used_elem *used_elem;
	uint16_t ring_idx;

	if (unlikely(!vq || !vq->used_ring))
		return;

	ring_idx = vq->last_used_idx % vq->queue_size;
	used_elem = &vq->used_ring->ring[ring_idx];

	used_elem->id = cpu_to_le32(desc_idx);
	used_elem->len = cpu_to_le32(len);

	atomic_thread_fence(memory_order_release);

	vq->last_used_idx++;
	vq->used_ring->idx = cpu_to_le16(vq->last_used_idx);
}

/**
 * virtqueue_destroy - release virtqueue memory
 * @vq: the virtqueue to destroy
 */
void virtqueue_destroy(struct virtqueue *vq)
{
	if (WARN_ON(!vq))
		return;

	free(vq);
}