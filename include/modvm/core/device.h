/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_DEVICE_H
#define MODVM_CORE_DEVICE_H

#include <stdint.h>
#include <modvm/utils/list.h>
#include <modvm/core/res_pool.h>

struct modvm_ctx;
struct modvm_device;
struct modvm_device_class;

/**
 * struct modvm_device_ops - standardized callbacks for hardware emulation
 * @realize: initialize hardware state and claim bus resources
 * @unrealize: release resources prior to destruction
 * @reset: restore the device to its power-on state
 * @read: handle a read request from the virtual processor
 * @write: handle a write request from the virtual processor
 */
struct modvm_device_ops {
	int (*realize)(struct modvm_device *dev);
	void (*unrealize)(struct modvm_device *dev);
	void (*reset)(struct modvm_device *dev);

	uint64_t (*read)(struct modvm_device *dev, uint64_t offset,
			 uint8_t size);
	void (*write)(struct modvm_device *dev, uint64_t offset, uint64_t val,
		      uint8_t size);
};

/**
 * struct modvm_device - base class for all virtual peripherals
 * @node: linked list node to attach to the context subsystem
 * @devm_pool: anchor for all dynamically allocated device resources
 * @ctx: the parent virtual machine context containing this device
 * @cls: pointer to the device blueprint
 * @name: human-readable identifier for debugging
 * @ops: pointer to the device implementation methods
 * @priv: opaque pointer for device-specific state to prevent arch leakage
 */
struct modvm_device {
	struct list_head node;
	struct modvm_res_pool devm_pool;

	struct modvm_ctx *ctx;
	const struct modvm_device_class *cls;
	const char *name;
	const struct modvm_device_ops *ops;
	void *priv;
};

/**
 * struct modvm_device_class - hardware implementation blueprint
 * @name: unique string identifier for the device type
 * @instantiate: factory callback to wire up internal routing and allocate state
 */
struct modvm_device_class {
	const char *name;
	int (*instantiate)(struct modvm_device *dev, void *pdata);
};

void modvm_device_class_register(const struct modvm_device_class *cls);
struct modvm_device *modvm_device_alloc(struct modvm_ctx *ctx,
					const char *name);
int modvm_device_add(struct modvm_device *dev, void *pdata);
void modvm_device_put(struct modvm_device *dev);

#endif /* MODVM_CORE_DEVICE_H */