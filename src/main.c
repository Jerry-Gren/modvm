/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <modvm/core/machine.h>
#include <modvm/core/loader.h>
#include <modvm/utils/log.h>
#include <modvm/core/character_device.h>
#include <modvm/backends/char/stdio.h>

#undef pr_fmt
#define pr_fmt(fmt) "hypervisor_main: " fmt

static void print_execution_usage(const char *program_name)
{
	fprintf(stderr, "Usage: %s [options]\n\n", program_name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr,
		"  -machine <name>  select emulated machine type (default: pc)\n");
	fprintf(stderr,
		"  -m <megabytes>   set guest ram size in mb (default: 16)\n");
	fprintf(stderr,
		"  -smp <cpus>      set number of virtual cpus (default: 1)\n");
	fprintf(stderr,
		"  -kernel <file>   load a flat binary firmware or kernel image\n");
	fprintf(stderr, "  -h               show this help message\n");
}

int main(int argument_count, char **argument_values)
{
	struct vm_machine virtual_machine;
	struct vm_machine_config machine_configuration = {
		.memory_base_address = 0x0000,
		.memory_size_bytes = 16 * 1024 * 1024,
		.processor_count = 1,
		.firmware_path = NULL,
		.machine_class = NULL,
		.primary_console_backend = NULL,
	};

	const char *requested_machine_name = "pc";
	int argument_index;
	int return_code;

	vm_log_initialize();

	pr_info("starting modvm hypervisor engine\n");

	for (argument_index = 1; argument_index < argument_count;
	     argument_index++) {
		if (strcmp(argument_values[argument_index], "-h") == 0) {
			print_execution_usage(argument_values[0]);
			return 0;
		} else if (strcmp(argument_values[argument_index],
				  "-machine") == 0 &&
			   argument_index + 1 < argument_count) {
			requested_machine_name =
				argument_values[++argument_index];
		} else if (strcmp(argument_values[argument_index], "-m") == 0 &&
			   argument_index + 1 < argument_count) {
			machine_configuration.memory_size_bytes =
				(size_t)atoi(
					argument_values[++argument_index]) *
				1024 * 1024;
		} else if (strcmp(argument_values[argument_index], "-smp") ==
				   0 &&
			   argument_index + 1 < argument_count) {
			machine_configuration.processor_count =
				(unsigned int)atoi(
					argument_values[++argument_index]);
		} else if (strcmp(argument_values[argument_index], "-kernel") ==
				   0 &&
			   argument_index + 1 < argument_count) {
			machine_configuration.firmware_path =
				argument_values[++argument_index];
		} else {
			pr_err("unknown option: %s\n",
			       argument_values[argument_index]);
			print_execution_usage(argument_values[0]);
			return EXIT_FAILURE;
		}
	}

	if (!machine_configuration.firmware_path) {
		pr_err("no firmware or kernel image specified. use -kernel <file>\n");
		return EXIT_FAILURE;
	}

	machine_configuration.machine_class =
		vm_machine_class_find(requested_machine_name);
	if (!machine_configuration.machine_class) {
		pr_err("unsupported machine type '%s'\n",
		       requested_machine_name);
		return EXIT_FAILURE;
	}

	/* instantiate the stdio backend object dynamically */
	machine_configuration.primary_console_backend =
		vm_character_device_stdio_create();
	if (!machine_configuration.primary_console_backend) {
		pr_err("failed to create standard io character backend\n");
		return EXIT_FAILURE;
	}

	return_code = vm_machine_init(&virtual_machine, &machine_configuration);
	if (return_code < 0) {
		pr_err("failed to initialize virtual machine context\n");
		goto error_cleanup_character_device;
	}

	return_code = vm_machine_run(&virtual_machine);
	if (return_code < 0) {
		pr_err("hypervisor runtime exited with fatal error\n");
		goto error_destroy_machine;
	}

	vm_machine_destroy(&virtual_machine);
	vm_character_device_stdio_destroy(
		machine_configuration.primary_console_backend);
	pr_info("hypervisor engine shutdown gracefully\n");

	vm_log_destroy();
	return EXIT_SUCCESS;

error_destroy_machine:
	vm_machine_destroy(&virtual_machine);
error_cleanup_character_device:
	vm_character_device_stdio_destroy(
		machine_configuration.primary_console_backend);
	vm_log_destroy();
	return EXIT_FAILURE;
}