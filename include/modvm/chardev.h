/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CHARDEV_H
#define MODVM_CHARDEV_H

#include <stddef.h>
#include <stdint.h>

struct vm_chardev;

/*
 * typedef chardev_recv_fn - callback invoked when the backend receives data
 * @opaque: pointer to the frontend device context (e.g., struct uart_state)
 * @buf: payload buffer
 * @len: number of bytes received
 */
typedef void (*chardev_recv_fn)(void *opaque, const uint8_t *buf, size_t len);

/**
 * struct vm_chardev_ops - character device backend operations
 * @write: push data from the virtual hardware to the host backend
 * @read: pull data from the host backend to the virtual hardware (future use)
 */
struct vm_chardev_ops {
	int (*write)(struct vm_chardev *dev, const uint8_t *buf, size_t len);
};

/**
 * struct vm_chardev - represents a host character stream backend
 * @name: identifier for debugging
 * @ops: dispatch table for backend operations
 * @private_data: backend-specific context
 */
struct vm_chardev {
	const char *name;
	const struct vm_chardev_ops *ops;
	void *private_data;

	chardev_recv_fn recv_cb;
	void *recv_opaque;
};

/**
 * chardev_set_receive_cb - bind a frontend device to receive backend data
 * @dev: the character device backend
 * @cb: the function to call upon data arrival
 * @opaque: the frontend context
 */
static inline void chardev_set_receive_cb(struct vm_chardev *dev,
					  chardev_recv_fn cb, void *opaque)
{
	if (dev) {
		dev->recv_cb = cb;
		dev->recv_opaque = opaque;
	}
}

struct vm_chardev *chardev_stdio_create(void);
void chardev_stdio_destroy(struct vm_chardev *dev);

#endif /* MODVM_CHARDEV_H */