/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/machine.h>
#include <modvm/core/device.h>
#include <modvm/utils/log.h>
#include <modvm/utils/compiler.h>
#include <modvm/core/loader.h>
#include <modvm/hw/serial.h>
#include <modvm/core/interrupt_line.h>

#undef pr_fmt
#define pr_fmt(fmt) "x86_pc: " fmt

struct pc_interrupt_route {
	struct vm_hypervisor *hypervisor;
	uint32_t global_interrupt_line;
};

static void pc_interrupt_handler(void *context_data, int level)
{
	struct pc_interrupt_route *route = context_data;
	vm_hypervisor_set_interrupt_line(route->hypervisor,
					 route->global_interrupt_line, level);
}

/**
 * machine_pc_initialize - assemble the legacy x86 personal computer topology
 * @machine: the machine instance being constructed
 *
 * Wires up standard legacy components such as the serial port at PIO 0x3f8
 * and custom hypervisor exit devices. Also initializes the hardware IRQ chips.
 *
 * return: 0 upon successful assembly, or a negative error code.
 */
static int machine_pc_initialize(struct vm_machine *machine)
{
	int return_code;
	struct vm_serial_platform_data serial_config;
	struct pc_interrupt_route *serial_route;

	return_code =
		vm_hypervisor_setup_interrupt_controller(&machine->hypervisor);
	if (return_code < 0) {
		pr_err("failed to initialize architectural interrupt routing\n");
		return return_code;
	}

	serial_route = calloc(1, sizeof(*serial_route));
	if (!serial_route)
		return -ENOMEM;

	serial_route->hypervisor = &machine->hypervisor;
	serial_route->global_interrupt_line = 4;

	serial_config.base_port_address = 0x3f8;
	serial_config.console_backend = machine->config.primary_console_backend;
	serial_config.interrupt_line =
		vm_interrupt_line_allocate(pc_interrupt_handler, serial_route);

	return_code = vm_device_create(machine, "uart-16550a", &serial_config);
	if (return_code < 0) {
		pr_err("failed to instantiate primary serial console\n");
		return return_code;
	}

	return_code = vm_device_create(machine, "debug-exit", NULL);
	if (return_code < 0) {
		pr_err("failed to instantiate debug exit device\n");
		return return_code;
	}

	return 0;
}

/**
 * machine_pc_reset - orchestrate the boot sequence for the PC architecture
 * @machine: the machine instance to reset
 *
 * Loads the firmware payload into memory and points the bootstrap processor
 * to the reset vector.
 *
 * return: 0 on success, or a negative error code.
 */
static int machine_pc_reset(struct vm_machine *machine)
{
	int return_code;
	uint64_t boot_instruction_pointer = 0x0000;

	if (machine->config.firmware_path) {
		return_code = vm_loader_load_raw_image(
			&machine->hypervisor.memory_space,
			machine->config.firmware_path,
			boot_instruction_pointer);
		if (return_code < 0) {
			pr_err("failed to map firmware payload: %s\n",
			       machine->config.firmware_path);
			return return_code;
		}
	}

	return_code = vm_virtual_cpu_set_instruction_pointer(
		machine->virtual_cpus[0], boot_instruction_pointer);
	if (return_code < 0) {
		pr_err("failed to configure bootstrap processor instruction pointer\n");
		return return_code;
	}

	return 0;
}

static const struct vm_machine_operations pc_machine_operations = {
	.init = machine_pc_initialize,
	.reset = machine_pc_reset,
};

static const struct vm_machine_class pc_machine_class = {
	.name = "pc",
	.description = "Standard x86 Personal Computer",
	.operations = &pc_machine_operations,
};

static void __attribute__((constructor)) register_pc_machine_class(void)
{
	vm_machine_class_register(&pc_machine_class);
}