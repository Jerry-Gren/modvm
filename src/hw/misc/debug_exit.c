/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/bus.h>
#include <modvm/core/machine.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>

#define DEBUG_EXIT_BASE_PORT 0x500

#undef pr_fmt
#define pr_fmt(fmt) "debug_exit: " fmt

struct debug_exit_state {
	struct vm_device device;
	struct vm_machine *machine_context;
};

static void debug_exit_write(struct vm_device *device, uint64_t offset,
			     uint64_t value, uint8_t access_size)
{
	struct debug_exit_state *state = device->private_data;

	(void)offset;
	(void)access_size;

	pr_info("guest requested power off (exit code 0x%lx)\n", value);
	vm_machine_request_shutdown(state->machine_context);
}

static const struct vm_device_operations debug_exit_operations = {
	.write = debug_exit_write,
};

/**
 * debug_exit_instantiate - create the debug exit hardware object
 * @machine: the topological container
 * @platform_data: unused for this specific device
 *
 * Registers a single I/O port that allows guest firmware to trigger
 * a graceful shutdown of the host virtualization engine.
 */
static int debug_exit_instantiate(struct vm_machine *machine,
				  void *platform_data)
{
	struct debug_exit_state *state;
	int return_code;

	(void)platform_data;

	state = calloc(1, sizeof(*state));
	if (!state)
		return -ENOMEM;

	state->machine_context = machine;

	state->device.name = "DEBUG_EXIT";
	state->device.operations = &debug_exit_operations;
	state->device.private_data = state;

	return_code = vm_bus_register_region(
		VM_BUS_SPACE_PORT_IO, DEBUG_EXIT_BASE_PORT, 1, &state->device);
	if (return_code < 0) {
		free(state);
		return return_code;
	}

	return 0;
}

static const struct vm_device_class debug_exit_class = {
	.name = "debug-exit",
	.instantiate = debug_exit_instantiate,
};

static void __attribute__((constructor)) register_debug_exit_class(void)
{
	vm_device_class_register(&debug_exit_class);
}