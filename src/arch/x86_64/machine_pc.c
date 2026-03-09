/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/machine.h>
#include <modvm/device.h>
#include <modvm/log.h>
#include <modvm/compiler.h>
#include <modvm/loader.h>
#include <modvm/hw/serial.h>
#include <modvm/irq.h>

/**
 * struct pc_gsi_route - routing context for the interrupt controller
 * @vm: hypervisor container capable of receiving the injection
 * @gsi: the target Global System Interrupt line
 */
struct pc_gsi_route {
	struct vm *vm;
	uint32_t gsi;
};

/**
 * pc_gsi_handler - callback triggered when a device asserts its IRQ pin
 */
static void pc_gsi_handler(void *opaque, int level)
{
	struct pc_gsi_route *route = opaque;
	vm_set_irq(route->vm, route->gsi, level);
}

/**
 * machine_pc_init - assembles the legacy x86 personal computer topology
 * @mach: the machine instance container
 *
 * Wires up standard legacy components such as the serial port at PIO 0x3f8
 * and custom hypervisor exit devices.
 */
static int machine_pc_init(struct machine *mach)
{
	int ret;
	struct serial_platform_data com1_data;
	struct pc_gsi_route *com1_route;

	ret = vm_arch_setup_irqchip(&mach->vm);
	if (ret < 0) {
		pr_err("Failed to initialize PC architectural interrupt routing\n");
		return ret;
	}

	/* Fabricate the interrupt route for COM1 to GSI 4 */
	com1_route = calloc(1, sizeof(*com1_route));
	if (!com1_route)
		return -ENOMEM;
	com1_route->vm = &mach->vm;
	com1_route->gsi = 4;

	/* Assemble the platform wiring harness */
	com1_data.base_port = 0x3f8;
	com1_data.chr = mach->config.serial_backend;
	com1_data.irq = vm_irq_allocate(pc_gsi_handler, com1_route);

	ret = vm_device_create(mach, "uart-16550a", &com1_data);
	if (ret < 0) {
		pr_err("Failed to instantiate primary UART device\n");
		return ret;
	}

	/* Request the debug exit device to allow graceful ACPI-like shutdown */
	ret = vm_device_create(mach, "debug-exit", NULL);
	if (ret < 0) {
		pr_err("Failed to instantiate debug exit device\n");
		return ret;
	}

	return 0;
}

/**
 * machine_pc_reset - orchestrate the boot sequence for the PC architecture
 * @mach: the machine instance
 */
static int machine_pc_reset(struct machine *mach)
{
	int ret;
	/*
	 * For full x86 fidelity, this would be 0xFFFFFFF0. We use 0x0000 here
	 * to match our current simple bare-metal test firmware design.
	 */
	uint64_t boot_ip = 0x0000;

	if (mach->config.kernel_path) {
		ret = loader_load_raw_image(&mach->vm.mem_space,
					    mach->config.kernel_path, boot_ip);
		if (ret < 0) {
			pr_err("Failed to load firmware payload: %s\n",
			       mach->config.kernel_path);
			return ret;
		}
	}

	/* Initialize the Bootstrap Processor (vCPU 0) to the reset vector */
	ret = vcpu_set_pc(mach->vcpus[0], boot_ip);
	if (ret < 0) {
		pr_err("Failed to initialize BSP instruction pointer\n");
		return ret;
	}

	return 0;
}

static const struct vm_machine_class pc_machine_class = {
	.name = "pc",
	.desc = "Standard x86 Personal Computer",
	.init = machine_pc_init,
	.reset = machine_pc_reset,
};

static void __attribute__((constructor)) register_pc_machine(void)
{
	vm_machine_class_register(&pc_machine_class);
}