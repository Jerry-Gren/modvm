/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <modvm/core/machine.h>
#include <modvm/core/loader.h>
#include <modvm/utils/log.h>
#include <modvm/core/chardev.h>
#include <modvm/backends/char/stdio.h>

#undef pr_fmt
#define pr_fmt(fmt) "main: " fmt

static void print_usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s [options]\n\n", prog_name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr,
		"  -machine <name>  select emulated machine type (default: pc)\n");
	fprintf(stderr,
		"  -m <megabytes>   set guest ram size in mb (default: 16)\n");
	fprintf(stderr,
		"  -smp <cpus>      set number of virtual cpus (default: 1)\n");
	fprintf(stderr,
		"  -kernel <file>   load a flat binary firmware or kernel image\n");
	fprintf(stderr,
		"  -accel <name>    select hypervisor backend (default: kvm)\n");
	fprintf(stderr, "  -h               show this help message\n");
}

int main(int argc, char **argv)
{
	struct vm_machine vm;
	struct vm_machine_config cfg = {
		.accel_name = "kvm",
		.ram_base = 0x0000,
		.ram_size = 16 * 1024 * 1024,
		.nr_vcpus = 1,
		.firmware_path = NULL,
		.machine_class = NULL,
		.console = NULL,
	};

	const char *mach_name = "pc";
	int i, ret;

	vm_log_initialize();
	pr_info("starting modvm hypervisor engine\n");

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) {
			print_usage(argv[0]);
			return 0;
		} else if (strcmp(argv[i], "-machine") == 0 && i + 1 < argc) {
			mach_name = argv[++i];
		} else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
			cfg.ram_size = (size_t)atoi(argv[++i]) * 1024 * 1024;
		} else if (strcmp(argv[i], "-smp") == 0 && i + 1 < argc) {
			cfg.nr_vcpus = (unsigned int)atoi(argv[++i]);
		} else if (strcmp(argv[i], "-kernel") == 0 && i + 1 < argc) {
			cfg.firmware_path = argv[++i];
		} else if (strcmp(argv[i], "-accel") == 0 && i + 1 < argc) {
			cfg.accel_name = argv[++i];
		} else {
			pr_err("unknown option: %s\n", argv[i]);
			print_usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (!cfg.firmware_path) {
		pr_err("no firmware or kernel image specified. use -kernel <file>\n");
		return EXIT_FAILURE;
	}

	cfg.machine_class = vm_machine_class_find(mach_name);
	if (!cfg.machine_class) {
		pr_err("unsupported machine type '%s'\n", mach_name);
		return EXIT_FAILURE;
	}

	cfg.console = vm_chardev_stdio_create();
	if (!cfg.console) {
		pr_err("failed to create standard io console backend\n");
		return EXIT_FAILURE;
	}

	ret = vm_machine_init(&vm, &cfg);
	if (ret < 0) {
		pr_err("failed to initialize virtual machine context\n");
		goto err_destroy_vm;
	}

	ret = vm_machine_run(&vm);
	if (ret < 0) {
		pr_err("hypervisor runtime exited with fatal error\n");
		goto err_destroy_vm;
	}

	vm_machine_destroy(&vm);
	vm_chardev_stdio_destroy(cfg.console);
	pr_info("hypervisor engine shutdown gracefully\n");

	vm_log_destroy();
	return EXIT_SUCCESS;

err_destroy_vm:
	vm_machine_destroy(&vm);
	vm_chardev_stdio_destroy(cfg.console);
	vm_log_destroy();
	return EXIT_FAILURE;
}