/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_NET_H
#define MODVM_CORE_NET_H

#include <stdint.h>
#include <sys/types.h>
#include <modvm/utils/stddef.h>

struct modvm_net;
struct modvm_event_loop;

/**
 * typedef modvm_net_rx_cb_t - callback for incoming network frames
 * @data: pointer to the frontend device context closure
 * @buf: payload buffer containing the incoming ethernet frame
 * @len: size of the ethernet frame in bytes
 */
typedef void (*modvm_net_rx_cb_t)(void *data, const uint8_t *buf, size_t len);

/**
 * struct modvm_net_ops - operations for host network backends
 * @write: inject an ethernet frame from the guest into the host network
 * @set_rx_cb: bind/unbind the frontend reception callback to the event loop
 * @get_mac: retrieve the hardware MAC address assigned to this backend
 * @release: ?
 */
struct modvm_net_ops {
	ssize_t (*write)(struct modvm_net *net, const uint8_t *buf, size_t len);
	void (*set_rx_cb)(struct modvm_net *net, struct modvm_event_loop *loop,
			  modvm_net_rx_cb_t cb, void *data);
	int (*get_mac)(struct modvm_net *net, uint8_t mac_out[6]);
	void (*release)(struct modvm_net *net);
};

/**
 * struct modvm_net - abstract base class for host network backends
 * @name: human-readable identifier for the network backend (e.g., "tap0")
 * @ops: dispatch table for backend operations
 * @priv: backend-specific operational context (e.g., file descriptors)
 * @rx_cb: registered frontend callback for asynchronous frame arrival
 * @rx_data: frontend context bound to the receive callback
 */
struct modvm_net {
	const char *name;
	const struct modvm_net_ops *ops;
	void *priv;

	modvm_net_rx_cb_t rx_cb;
	void *rx_data;
};

struct modvm_net_driver {
	const char *name;
	struct modvm_net *(*create)(const char *opts);
};

void modvm_net_driver_register(const struct modvm_net_driver *drv);
struct modvm_net *modvm_net_create(const char *name, const char *opts);
void modvm_net_release(struct modvm_net *net);

/**
 * modvm_net_set_rx_cb - register the reception hook for a network backend
 * @net: the abstract network backend instance
 * @ctx: the global virtual machine context (for event loop registration)
 * @cb: the function to invoke upon frame arrival
 * @data: opaque closure payload
 */
static inline void modvm_net_set_rx_cb(struct modvm_net *net,
				       struct modvm_event_loop *loop,
				       modvm_net_rx_cb_t cb, void *data)
{
	if (net) {
		net->rx_cb = cb;
		net->rx_data = data;
		if (net->ops && net->ops->set_rx_cb)
			net->ops->set_rx_cb(net, loop, cb, data);
	}
}

#endif /* MODVM_CORE_NET_H */