/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <modvm/core/modvm.h>
#include <modvm/core/board.h>
#include <modvm/utils/log.h>
#include <modvm/core/chardev.h>
#include <modvm/backends/char/stdio.h>

#undef pr_fmt
#define pr_fmt(fmt) "main: " fmt

/**
 * print_usage - display command line interface documentation
 * @prog_name: the executable name invoked by the user
 */
static void print_usage(const char *prog_name)
{
	fprintf(stderr, "Usage: %s [options]\n\n", prog_name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr,
		"  -board <name>       select emulated motherboard (default: pc)\n");
	fprintf(stderr,
		"  -m <megabytes>      set guest ram size in mb (default: 16)\n");
	fprintf(stderr,
		"  -smp <cpus>         set number of virtual cpus (default: 1)\n");
	fprintf(stderr,
		"  -accel <name>       select hypervisor backend (default: kvm)\n");
	fprintf(stderr,
		"  -loader <name>      select boot protocol plugin (default: raw-x86)\n");
	fprintf(stderr,
		"  -loader-opts <opts> pass configuration string to the loader plugin\n");
	fprintf(stderr, "  -h                  show this help message\n");
}

/**
 * main - primary entry point for the virtualization engine
 * @argc: argument count
 * @argv: argument vector
 *
 * Parses user configurations, provisions the requested hardware topology
 * and bootloaders, and transitions execution to the hypervisor core.
 *
 * Return: EXIT_SUCCESS on graceful shutdown, EXIT_FAILURE on fatal errors.
 */
int main(int argc, char **argv)
{
	struct modvm_ctx vm;
	struct modvm_config cfg = {
		.accel_name = "kvm",
		.ram_base = 0x0000,
		.ram_size = 16 * 1024 * 1024,
		.nr_vcpus = 1,
		.loader_name = "raw-x86",
		.loader_opts = NULL,
		.board = NULL,
		.console = NULL,
	};

	const char *board_name = "pc";
	int i;
	int ret;

	modvm_log_initialize();
	pr_info("starting modvm hypervisor engine\n");

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) {
			print_usage(argv[0]);
			return EXIT_SUCCESS;
		} else if (strcmp(argv[i], "-board") == 0 && i + 1 < argc) {
			board_name = argv[++i];
		} else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
			cfg.ram_size = (size_t)atoi(argv[++i]) * 1024 * 1024;
		} else if (strcmp(argv[i], "-smp") == 0 && i + 1 < argc) {
			cfg.nr_vcpus = (unsigned int)atoi(argv[++i]);
		} else if (strcmp(argv[i], "-accel") == 0 && i + 1 < argc) {
			cfg.accel_name = argv[++i];
		} else if (strcmp(argv[i], "-loader") == 0 && i + 1 < argc) {
			cfg.loader_name = argv[++i];
		} else if (strcmp(argv[i], "-loader-opts") == 0 &&
			   i + 1 < argc) {
			cfg.loader_opts = argv[++i];
		} else {
			pr_err("unknown or incomplete option: %s\n", argv[i]);
			print_usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (!cfg.loader_opts) {
		pr_warn("no loader options specified, processor may lack a boot payload\n");
	}

	cfg.board = modvm_board_find(board_name);
	if (!cfg.board) {
		pr_err("unsupported motherboard type '%s'\n", board_name);
		return EXIT_FAILURE;
	}

	cfg.console = modvm_chardev_stdio_create();
	if (!cfg.console) {
		pr_err("failed to create standard io console backend\n");
		return EXIT_FAILURE;
	}

	ret = modvm_init(&vm, &cfg);
	if (ret < 0) {
		pr_err("failed to initialize virtual machine context\n");
		goto err_destroy_vm;
	}

	ret = modvm_run(&vm);
	if (ret < 0) {
		pr_err("hypervisor runtime exited with fatal error\n");
		goto err_destroy_vm;
	}

	modvm_destroy(&vm);
	modvm_chardev_stdio_destroy(cfg.console);
	pr_info("hypervisor engine shutdown gracefully\n");

	modvm_log_destroy();
	return EXIT_SUCCESS;

err_destroy_vm:
	/* Trigger the context-managed cleanup loop to catch partial allocations */
	modvm_destroy(&vm);
	if (cfg.console)
		modvm_chardev_stdio_destroy(cfg.console);
	modvm_log_destroy();
	return EXIT_FAILURE;
}