/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/bus.h>
#include <modvm/machine.h>
#include <modvm/log.h>
#include <modvm/err.h>

#define DEBUG_EXIT_PORT 0x500

#undef pr_fmt
#define pr_fmt(fmt) "debug_exit: " fmt

/**
 * struct debug_exit - hardware state for the graceful shutdown device
 * @dev: base virtual device structure
 * @mach: pointer to the core machine context to trigger global shutdown
 */
struct debug_exit {
	struct vm_device dev;
	struct machine *mach;
};

/**
 * debug_exit_write - handle CPU outb instructions targeting this device
 *
 * When the guest kernel writes to the debug port, we intercept the
 * payload as an exit code and request the hypervisor to halt execution.
 */
static void debug_exit_write(struct vm_device *dev, uint64_t offset,
			     uint64_t value, uint8_t size)
{
	struct debug_exit *de = dev->private_data;

	/*
	 * Suppress unused parameter warnings for parameters not required
	 * by this specific hardware implementation.
	 */
	(void)offset;
	(void)size;

	pr_info("Guest requested ACPI-like shutdown (exit code 0x%lx)\n",
		value);
	machine_request_shutdown(de->mach);
}

static const struct vm_device_ops debug_exit_ops = {
	.write = debug_exit_write,
};

static int debug_exit_create(struct machine *mach, void *platform_data)
{
	struct debug_exit *de;
	int ret;

	(void)platform_data;

	de = calloc(1, sizeof(*de));
	if (!de)
		return -ENOMEM;

	de->mach = mach;

	de->dev.name = "DEBUG_EXIT";
	de->dev.ops = &debug_exit_ops;
	de->dev.private_data = de;

	/* Register the device specifically to the PIO address space */
	ret = bus_register_region(VM_BUS_SPACE_PIO, DEBUG_EXIT_PORT, 1,
				  &de->dev);
	if (ret < 0) {
		free(de);
		return ret;
	}

	return 0;
}

static const struct vm_device_class debug_exit_class = {
	.name = "debug-exit",
	.create = debug_exit_create,
};

static void __attribute__((constructor)) register_debug_exit(void)
{
	vm_device_class_register(&debug_exit_class);
}