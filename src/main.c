/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <modvm/machine.h>
#include <modvm/loader.h>
#include <modvm/log.h>
#include <modvm/chardev.h>

#undef pr_fmt
#define pr_fmt(fmt) "main: " fmt

static void print_usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s [options]\n\n", prog_name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr,
		"  -machine <name>  Select emulated machine type (default: pc)\n");
	fprintf(stderr,
		"  -m <megabytes>   Set guest RAM size in MB (default: 16)\n");
	fprintf(stderr,
		"  -smp <cpus>      Set number of virtual CPUs (default: 1)\n");
	fprintf(stderr,
		"  -kernel <file>   Load a flat binary firmware/kernel image\n");
	fprintf(stderr, "  -h               Show this help message\n");
}

int main(int argc, char **argv)
{
	struct machine mach;
	struct machine_config cfg = {
		.ram_base = 0x0000,
		.ram_size = 16 * 1024 * 1024,
		.smp_cpus = 1,
		.kernel_path = NULL,
		.machine_type = NULL,
		.serial_backend = NULL,
	};
	const char *machine_name = "pc";
	int i;
	int ret;

	pr_info("Starting ModVM Hypervisor\n");

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) {
			print_usage(argv[0]);
			return 0;
		} else if (strcmp(argv[i], "-machine") == 0 && i + 1 < argc) {
			machine_name = argv[++i];
		} else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
			cfg.ram_size = (size_t)atoi(argv[++i]) * 1024 * 1024;
		} else if (strcmp(argv[i], "-smp") == 0 && i + 1 < argc) {
			cfg.smp_cpus = (unsigned int)atoi(argv[++i]);
		} else if (strcmp(argv[i], "-kernel") == 0 && i + 1 < argc) {
			cfg.kernel_path = argv[++i];
		} else {
			pr_err("Unknown option: %s\n", argv[i]);
			print_usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (!cfg.kernel_path) {
		pr_err("No firmware or kernel image specified. Use -kernel <file>\n");
		return EXIT_FAILURE;
	}

	cfg.machine_type = vm_machine_class_find(machine_name);
	if (!cfg.machine_type) {
		pr_err("Unsupported machine type '%s'\n", machine_name);
		return EXIT_FAILURE;
	}

	/* Instantiate the host backend resource for serial output */
	cfg.serial_backend = chardev_stdio_create();
	if (!cfg.serial_backend) {
		pr_err("Failed to create standard I/O character backend\n");
		return EXIT_FAILURE;
	}

	ret = machine_init(&mach, &cfg);
	if (ret < 0) {
		pr_err("Failed to initialize virtual machine context\n");
		goto err_cleanup_chardev;
	}

	ret = machine_run(&mach);
	if (ret < 0) {
		pr_err("Hypervisor runtime exited with fatal error\n");
		goto err_destroy_machine;
	}

	machine_destroy(&mach);
	chardev_stdio_destroy(cfg.serial_backend);
	pr_info("Hypervisor engine shutdown gracefully\n");

	return EXIT_SUCCESS;

err_destroy_machine:
	machine_destroy(&mach);
err_cleanup_chardev:
	chardev_stdio_destroy(cfg.serial_backend);
	return EXIT_FAILURE;
}