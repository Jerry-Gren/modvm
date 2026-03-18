/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_CHARDEV_H
#define MODVM_CORE_CHARDEV_H

#include <stdint.h>
#include <modvm/utils/stddef.h>

struct modvm_chardev;
struct modvm_event_loop;

/**
 * typedef modvm_chardev_rx_cb_t - callback for incoming data stream
 * @data: pointer to the frontend device context closure
 * @buf: payload buffer containing the incoming byte stream
 * @len: number of bytes received in the buffer
 */
typedef void (*modvm_chardev_rx_cb_t)(void *data, const uint8_t *buf,
				      size_t len);

/**
 * struct modvm_chardev_ops - operations for character device backends
 * @write: push data from the virtual hardware frontend to the host backend
 * @set_rx_cb: bind/unbind the frontend reception callback to the event loop
 * @pause_rx: ?
 * @resume_rx: ?
 * @release: ?
 */
struct modvm_chardev_ops {
	int (*write)(struct modvm_chardev *dev, const uint8_t *buf, size_t len);
	void (*set_rx_cb)(struct modvm_chardev *dev,
			  struct modvm_event_loop *loop,
			  modvm_chardev_rx_cb_t cb, void *data);
	void (*pause_rx)(struct modvm_chardev *dev);
	void (*resume_rx)(struct modvm_chardev *dev);
	void (*release)(struct modvm_chardev *dev);
};

/**
 * struct modvm_chardev - represents a host character stream backend
 * @name: identifier used for debugging and tracing
 * @ops: dispatch table for backend operations
 * @priv: backend-specific operational context (e.g., termios states)
 * @rx_cb: registered frontend callback for asynchronous data arrival
 * @rx_data: frontend context bound to the receive callback
 */
struct modvm_chardev {
	const char *name;
	const struct modvm_chardev_ops *ops;
	void *priv;

	modvm_chardev_rx_cb_t rx_cb;
	void *rx_data;
};

struct modvm_chardev_driver {
	const char *name;
	struct modvm_chardev *(*create)(const char *opts);
};

void modvm_chardev_driver_register(const struct modvm_chardev_driver *drv);
struct modvm_chardev *modvm_chardev_create(const char *name, const char *opts);
void modvm_chardev_release(struct modvm_chardev *dev);

static inline void modvm_chardev_set_rx_cb(struct modvm_chardev *dev,
					   struct modvm_event_loop *loop,
					   modvm_chardev_rx_cb_t cb, void *data)
{
	if (dev) {
		dev->rx_cb = cb;
		dev->rx_data = data;
		if (dev->ops && dev->ops->set_rx_cb)
			dev->ops->set_rx_cb(dev, loop, cb, data);
	}
}

static inline void modvm_chardev_pause_rx(struct modvm_chardev *dev)
{
	if (dev && dev->ops && dev->ops->pause_rx)
		dev->ops->pause_rx(dev);
}

static inline void modvm_chardev_resume_rx(struct modvm_chardev *dev)
{
	if (dev && dev->ops && dev->ops->resume_rx)
		dev->ops->resume_rx(dev);
}

#endif /* MODVM_CORE_CHARDEV_H */