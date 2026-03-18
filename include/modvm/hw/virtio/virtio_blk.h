/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_VIRTIO_VIRTIO_BLK_H
#define MODVM_HW_VIRTIO_VIRTIO_BLK_H

struct modvm_ctx;
struct modvm_block;
struct virtio_device;

struct virtio_device *virtio_blk_create(struct modvm_ctx *ctx,
					struct modvm_block *backend);

#endif /* MODVM_HW_VIRTIO_VIRTIO_BLK_H */