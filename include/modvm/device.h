/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_DEVICE_H
#define MODVM_DEVICE_H

#include <stdint.h>

#include <modvm/machine.h>

struct vm_device;

/**
 * struct vm_device_ops - standardized callbacks for hardware emulation
 * @realize: initialize hardware state and claim resources (memory, interrupts)
 * @unrealize: release resources prior to destruction
 * @reset: restore the device to its power-on state
 * @read: handle a read request from the CPU. The address is relative
 * to the device's base address, not the absolute system address.
 * @write: handle a write request from the CPU.
 *
 * This structure isolates the core virtual machine from the specific
 * behaviors of individual hardware peripherals.
 */
struct vm_device_ops {
	int (*realize)(struct vm_device *dev);
	void (*unrealize)(struct vm_device *dev);
	void (*reset)(struct vm_device *dev);

	uint64_t (*read)(struct vm_device *dev, uint64_t offset, uint8_t size);
	void (*write)(struct vm_device *dev, uint64_t offset, uint64_t value,
		      uint8_t size);
};

/**
 * struct vm_device - base class for all virtual peripherals
 * @name: human-readable identifier for debugging
 * @ops: pointer to the device's implementation methods
 * @private_data: opaque pointer for device-specific state structures
 */
struct vm_device {
	const char *name;
	const struct vm_device_ops *ops;
	void *private_data;
};

/**
 * struct vm_device_class - device blueprint for the factory
 * @name: unique string identifier for the device type
 * @create: factory callback to instantiate the device onto a machine
 *
 * This enables dependency injection. Board code can request devices
 * by string name without compile-time linkage.
 */
struct vm_device_class {
	const char *name;
	int (*create)(struct machine *mach, void *platform_data);
};

void vm_device_class_register(const struct vm_device_class *cls);

int vm_device_create(struct machine *mach, const char *name,
		     void *platform_data);

#endif /* MODVM_DEVICE_H */