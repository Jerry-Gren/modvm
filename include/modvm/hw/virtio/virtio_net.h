/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_HW_VIRTIO_VIRTIO_NET_H
#define MODVM_HW_VIRTIO_VIRTIO_NET_H

struct modvm_ctx;
struct modvm_net;
struct virtio_device;

struct virtio_device *virtio_net_create(struct modvm_ctx *ctx,
					struct modvm_net *backend);

#endif /* MODVM_HW_VIRTIO_VIRTIO_NET_H */