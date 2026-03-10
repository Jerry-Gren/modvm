/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <modvm/core/machine.h>
#include <modvm/core/device.h>
#include <modvm/core/memory.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>
#include <modvm/core/character_device.h>
#include <modvm/core/interrupt_line.h>
#include <modvm/hw/serial.h>
#include <modvm/backends/char/stdio.h>

#undef pr_fmt
#define pr_fmt(fmt) "test_machine: " fmt

static const uint8_t bare_metal_guest_payload[] = {
	0xba, 0xf8, 0x03, 0xb0, 0x4f, 0xee, 0xb0, 0x4b, 0xee, 0xb0, 0x0d,
	0xee, 0xb0, 0x0a, 0xee, 0xba, 0x00, 0x05, 0xb0, 0x01, 0xee, 0xf4
};

struct mock_interrupt_route {
	struct vm_hypervisor *hypervisor;
	uint32_t global_interrupt_line;
};

static void mock_interrupt_handler(void *context_data, int level)
{
	struct mock_interrupt_route *route = context_data;
	vm_hypervisor_set_interrupt_line(route->hypervisor,
					 route->global_interrupt_line, level);
}

static int mock_machine_initialize(struct vm_machine *machine)
{
	int return_code;
	struct vm_serial_platform_data serial_configuration;
	struct mock_interrupt_route *serial_interrupt_route;

	return_code =
		vm_hypervisor_setup_interrupt_controller(&machine->hypervisor);
	if (return_code < 0) {
		pr_err("failed to initialize architectural interrupt controller\n");
		return return_code;
	}

	serial_interrupt_route = calloc(1, sizeof(*serial_interrupt_route));
	if (!serial_interrupt_route)
		return -ENOMEM;
	serial_interrupt_route->hypervisor = &machine->hypervisor;
	serial_interrupt_route->global_interrupt_line = 4;

	serial_configuration.base_port_address = 0x3f8;
	serial_configuration.console_backend =
		machine->config.primary_console_backend;
	serial_configuration.interrupt_line = vm_interrupt_line_allocate(
		mock_interrupt_handler, serial_interrupt_route);

	return_code =
		vm_device_create(machine, "uart-16550a", &serial_configuration);
	if (return_code < 0) {
		pr_err("failed to probe uart peripheral\n");
		return return_code;
	}

	return_code = vm_device_create(machine, "debug-exit", NULL);
	if (return_code < 0) {
		pr_err("failed to probe debug exit device\n");
		return return_code;
	}

	pr_info("mock hardware topology injected successfully\n");
	return 0;
}

static int mock_machine_reset(struct vm_machine *machine)
{
	void *host_virtual_address;
	int return_code;

	host_virtual_address = vm_memory_guest_to_host_address(
		&machine->hypervisor.memory_space, 0x0000);
	if (IS_ERR_OR_NULL(host_virtual_address)) {
		pr_err("failed to translate address 0x0000 for payload injection\n");
		return -EFAULT;
	}

	memcpy(host_virtual_address, bare_metal_guest_payload,
	       sizeof(bare_metal_guest_payload));
	pr_info("injected %zu bytes of machine code into guest memory\n",
		sizeof(bare_metal_guest_payload));

	return_code = vm_virtual_cpu_set_instruction_pointer(
		machine->virtual_cpus[0], 0x0000);
	if (return_code < 0)
		return return_code;

	return 0;
}

static const struct vm_machine_operations mock_machine_operations = {
	.init = mock_machine_initialize,
	.reset = mock_machine_reset,
};

static const struct vm_machine_class mock_machine_class = {
	.name = "mock",
	.description = "Mock machine for integration testing",
	.operations = &mock_machine_operations,
};

static void execute_machine_lifecycle_test(void)
{
	struct vm_machine virtual_machine;
	struct vm_character_device *console_backend;
	int return_code;

	console_backend = vm_character_device_stdio_create();
	if (WARN_ON(!console_backend))
		vm_panic("failed to create character backend\n");

	struct vm_machine_config configuration = {
		.memory_base_address = 0x0000,
		.memory_size_bytes = 4096,
		.processor_count = 1,
		.firmware_path = NULL,
		.machine_class = &mock_machine_class,
		.primary_console_backend = console_backend,
	};

	pr_info("initiating motherboard initialization sequence\n");

	return_code = vm_machine_init(&virtual_machine, &configuration);
	if (WARN_ON(return_code < 0))
		vm_panic("machine assembly failed\n");

	pr_info("igniting processor cores\n");

	return_code = vm_machine_run(&virtual_machine);
	if (WARN_ON(return_code < 0))
		vm_panic("hypervisor runtime encountered fatal error\n");

	pr_info("tearing down virtualization context\n");
	vm_machine_destroy(&virtual_machine);
	vm_character_device_stdio_destroy(console_backend);
}

int main(void)
{
	pr_info("Initiating ModVM machine architecture integration test\n");
	execute_machine_lifecycle_test();
	pr_info("SUCCESS: machine architecture test concluded gracefully\n");
	return 0;
}