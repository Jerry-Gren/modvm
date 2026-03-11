/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_CHARDEV_H
#define MODVM_CORE_CHARDEV_H

#include <stdint.h>
#include <modvm/utils/stddef.h>

struct vm_chardev;
struct vm_machine;

/**
 * typedef vm_chardev_rx_cb_t - callback for incoming data stream.
 * @data: pointer to the frontend device context.
 * @buf: payload buffer containing the incoming byte stream.
 * @len: number of bytes received in the buffer.
 */
typedef void (*vm_chardev_rx_cb_t)(void *data, const uint8_t *buf, size_t len);

/**
 * struct vm_chardev_ops - operations for character device backends.
 * @write: push data from the virtual hardware frontend to the host backend.
 * @set_rx_cb: bind/unbind the frontend reception callback to the event loop.
 */
struct vm_chardev_ops {
	int (*write)(struct vm_chardev *dev, const uint8_t *buf, size_t len);
	void (*set_rx_cb)(struct vm_chardev *dev, struct vm_machine *machine,
			  vm_chardev_rx_cb_t cb, void *data);
};

/**
 * struct vm_chardev - represents a host character stream backend.
 * @name: identifier used for debugging and tracing.
 * @ops: dispatch table for backend operations.
 * @priv: backend-specific operational context.
 * @rx_cb: registered frontend callback for asynchronous data arrival.
 * @rx_data: frontend context bound to the receive callback.
 */
struct vm_chardev {
	const char *name;
	const struct vm_chardev_ops *ops;
	void *priv;

	vm_chardev_rx_cb_t rx_cb;
	void *rx_data;
};

/**
 * vm_chardev_set_rx_cb - bind a frontend to receive backend data.
 * @dev: the character device backend instance.
 * @machine: the machine context providing the event loop.
 * @cb: the function to invoke upon data arrival.
 * @data: the frontend context to pass to the callback.
 */
static inline void vm_chardev_set_rx_cb(struct vm_chardev *dev,
					struct vm_machine *machine,
					vm_chardev_rx_cb_t cb, void *data)
{
	if (dev) {
		dev->rx_cb = cb;
		dev->rx_data = data;
		if (dev->ops && dev->ops->set_rx_cb)
			dev->ops->set_rx_cb(dev, machine, cb, data);
	}
}

#endif /* MODVM_CORE_CHARDEV_H */