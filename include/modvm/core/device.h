/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_DEVICE_H
#define MODVM_CORE_DEVICE_H

#include <stdint.h>
#include <modvm/core/machine.h>
#include <modvm/utils/list.h>
#include <modvm/core/res_pool.h>

struct vm_device;
struct vm_device_class;

/**
 * struct vm_device_ops - standardized callbacks for hardware emulation.
 * @realize: initialize hardware state and claim bus resources.
 * @unrealize: release resources prior to destruction.
 * @reset: restore the device to its power-on state.
 * @read: handle a read request from the virtual processor.
 * @write: handle a write request from the virtual processor.
 */
struct vm_device_ops {
	int (*realize)(struct vm_device *dev);
	void (*unrealize)(struct vm_device *dev);
	void (*reset)(struct vm_device *dev);

	uint64_t (*read)(struct vm_device *dev, uint64_t offset, uint8_t size);
	void (*write)(struct vm_device *dev, uint64_t offset, uint64_t val,
		      uint8_t size);
};

/**
 * struct vm_device - base class for all virtual peripherals.
 * @node: linked list node to attach to the machine subsystem.
 * @devres_head: anchor for all dynamically allocated device resources.
 * @machine: the parent machine topology containing this device.
 * @name: human-readable identifier for debugging.
 * @ops: pointer to the device implementation methods.
 * @priv: opaque pointer for device-specific state.
 */
struct vm_device {
	struct list_head node;
	struct vm_res_pool devres_pool;

	struct vm_machine *machine;
	const struct vm_device_class *cls;
	const char *name;
	const struct vm_device_ops *ops;
	void *priv;
};

/**
 * struct vm_device_class - hardware implementation blueprint.
 * @name: unique string identifier for the device type.
 * @instantiate: factory callback to wire up internal routing and allocate state.
 */
struct vm_device_class {
	const char *name;
	int (*instantiate)(struct vm_device *dev, void *pdata);
};

void vm_device_class_register(const struct vm_device_class *cls);

struct vm_device *vm_device_alloc(struct vm_machine *machine, const char *name);
int vm_device_add(struct vm_device *dev, void *pdata);
void vm_device_put(struct vm_device *dev);

#endif /* MODVM_CORE_DEVICE_H */