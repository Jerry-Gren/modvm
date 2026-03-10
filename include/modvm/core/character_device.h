/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_CHARACTER_DEVICE_H
#define MODVM_CORE_CHARACTER_DEVICE_H

#include <stddef.h>
#include <stdint.h>

struct vm_character_device;

/**
 * typedef vm_character_device_receive_callback_t - callback for incoming data stream
 * @data: pointer to the frontend device context
 * @buffer: payload buffer containing the incoming byte stream
 * @length: number of bytes received in the buffer
 */
typedef void (*vm_character_device_receive_callback_t)(void *data,
						       const uint8_t *buffer,
						       size_t length);

/**
 * struct vm_character_device_operations - operations for character device backends
 * @write: push data from the virtual hardware frontend to the host backend
 */
struct vm_character_device_operations {
	int (*write)(struct vm_character_device *device, const uint8_t *buffer,
		     size_t length);
};

/**
 * struct vm_character_device - represents a host character stream backend
 * @name: identifier used for debugging and tracing
 * @operations: dispatch table for backend operations
 * @private_data: backend-specific operational context
 * @receive_callback: registered frontend callback for asynchronous data arrival
 * @receive_data: frontend context bound to the receive callback
 */
struct vm_character_device {
	const char *name;
	const struct vm_character_device_operations *operations;
	void *private_data;

	vm_character_device_receive_callback_t receive_callback;
	void *receive_data;
};

/**
 * vm_character_device_set_receive_callback - bind a frontend to receive backend data
 * @device: the character device backend instance
 * @callback: the function to invoke upon data arrival
 * @data: the frontend context to pass to the callback
 */
static inline void vm_character_device_set_receive_callback(
	struct vm_character_device *device,
	vm_character_device_receive_callback_t callback, void *data)
{
	if (device) {
		device->receive_callback = callback;
		device->receive_data = data;
	}
}

#endif /* MODVM_CORE_CHARACTER_DEVICE_H */