/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_DEVICE_H
#define MODVM_CORE_DEVICE_H

#include <stdint.h>
#include <modvm/core/machine.h>

struct vm_device;
struct vm_machine;

/**
 * struct vm_device_operations - standardized callbacks for hardware emulation
 * @realize: initialize hardware state and claim bus resources
 * @unrealize: release resources prior to destruction
 * @reset: restore the device to its power-on state
 * @read: handle a read request from the virtual processor
 * @write: handle a write request from the virtual processor
 */
struct vm_device_operations {
	int (*realize)(struct vm_device *device);
	void (*unrealize)(struct vm_device *device);
	void (*reset)(struct vm_device *device);

	uint64_t (*read)(struct vm_device *device, uint64_t offset,
			 uint8_t access_size);
	void (*write)(struct vm_device *device, uint64_t offset, uint64_t value,
		      uint8_t access_size);
};

/**
 * struct vm_device - base class for all virtual peripherals
 * @name: human-readable identifier for debugging
 * @operations: pointer to the device implementation methods
 * @private_data: opaque pointer for device-specific state
 */
struct vm_device {
	const char *name;
	const struct vm_device_operations *operations;
	void *private_data;
};

/**
 * struct vm_device_class - hardware implementation blueprint
 * @name: unique string identifier for the device type
 * @instantiate: factory callback to create the device onto a machine
 *
 * Enables dependency injection by allowing board code to request
 * devices by name rather than static compile-time linkage.
 */
struct vm_device_class {
	const char *name;
	int (*instantiate)(struct vm_machine *machine, void *platform_data);
};

void vm_device_class_register(const struct vm_device_class *device_class);

int vm_device_create(struct vm_machine *machine, const char *name,
		     void *platform_data);

#endif /* MODVM_CORE_DEVICE_H */