/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <modvm/machine.h>
#include <modvm/device.h>
#include <modvm/memory.h>
#include <modvm/log.h>
#include <modvm/err.h>
#include <modvm/bug.h>
#include <modvm/chardev.h>
#include <modvm/irq.h>
#include <modvm/hw/serial.h>

#undef pr_fmt
#define pr_fmt(fmt) "test_machine: " fmt

/*
 * The raw machine code payload for the test environment.
 * It outputs "OK\r\n" to the serial port, writes to the debug
 * exit port to gracefully stop the hypervisor, and halts.
 */
static const uint8_t guest_payload[] = {
	0xba, 0xf8, 0x03, /* mov dx, 0x3f8 */
	0xb0, 0x4f, /* mov al, 'O' */
	0xee, /* out dx, al */
	0xb0, 0x4b, /* mov al, 'K' */
	0xee, /* out dx, al */
	0xb0, 0x0d, /* mov al, '\r' */
	0xee, /* out dx, al */
	0xb0, 0x0a, /* mov al, '\n' */
	0xee, /* out dx, al */
	0xba, 0x00, 0x05, /* mov dx, 0x500 */
	0xb0, 0x01, /* mov al, 0x01 */
	0xee, /* out dx, al */
	0xf4 /* hlt */
};

/**
 * struct mock_gsi_route - routing context for the interrupt controller
 * @vm: hypervisor container
 * @gsi: target Global System Interrupt
 */
struct mock_gsi_route {
	struct vm *vm;
	uint32_t gsi;
};

/**
 * mock_gsi_handler - callback triggered when a device asserts its IRQ pin
 */
static void mock_gsi_handler(void *opaque, int level)
{
	struct mock_gsi_route *route = opaque;
	vm_set_irq(route->vm, route->gsi, level);
}

/**
 * mock_machine_init - Inject peripherals into the machine bus
 * @mach: The machine instance being assembled
 */
static int mock_machine_init(struct machine *mach)
{
	int ret;
	struct serial_platform_data com1_data;
	struct mock_gsi_route *com1_route;

	ret = vm_arch_setup_irqchip(&mach->vm);
	if (ret < 0) {
		pr_err("Failed to initialize PC architectural interrupt routing\n");
		return ret;
	}

	/* Fabricate the interrupt route for the mock environment */
	com1_route = calloc(1, sizeof(*com1_route));
	if (!com1_route)
		return -ENOMEM;
	com1_route->vm = &mach->vm;
	com1_route->gsi = 4;

	/* Assemble the platform wiring harness */
	com1_data.base_port = 0x3f8;
	com1_data.chr = mach->config.serial_backend;
	com1_data.irq = vm_irq_allocate(mock_gsi_handler, com1_route);

	ret = vm_device_create(mach, "uart-16550a", &com1_data);
	if (ret < 0) {
		pr_err("Failed to instantiate UART peripheral\n");
		return ret;
	}

	/*
	 * We must instantiate the debug exit device, otherwise the test payload
	 * will write to an unmapped port and the event loop will hang forever.
	 */
	ret = vm_device_create(mach, "debug-exit", NULL);
	if (ret < 0) {
		pr_err("Failed to initialize debug exit device\n");
		return ret;
	}

	pr_info("Hardware peripherals injected successfully\n");
	return 0;
}

/**
 * mock_machine_reset - encapsulate firmware loading for the test
 */
static int mock_machine_reset(struct machine *mach)
{
	void *hva;
	int ret;

	hva = vm_memory_gpa_to_hva(&mach->vm.mem_space, 0x0000);
	if (IS_ERR_OR_NULL(hva)) {
		pr_err("Failed to translate GPA 0x0000 for loading\n");
		return -EFAULT;
	}

	memcpy(hva, guest_payload, sizeof(guest_payload));
	pr_info("Loaded %zu bytes of payload into guest RAM\n",
		sizeof(guest_payload));

	ret = vcpu_set_pc(mach->vcpus[0], 0x0000);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct vm_machine_class mock_machine_class = {
	.name = "mock",
	.desc = "Mock machine for testing",
	.init = mock_machine_init,
	.reset = mock_machine_reset,
};

static void test_machine_lifecycle(void)
{
	struct machine mach;
	struct vm_chardev *chr;
	int ret;

	chr = chardev_stdio_create();
	if (WARN_ON(!chr))
		vm_panic("Failed to create character backend\n");

	struct machine_config cfg = {
		.ram_base = 0x0000,
		.ram_size = 4096,
		.smp_cpus = 1,
		.kernel_path = NULL,
		.machine_type = &mock_machine_class,
		.serial_backend = chr,
	};

	pr_info("Starting machine motherboard initialization...\n");

	ret = machine_init(&mach, &cfg);
	if (WARN_ON(ret < 0))
		vm_panic("Machine initialization failed\n");

	pr_info("Igniting processor cores...\n");

	ret = machine_run(&mach);
	if (WARN_ON(ret < 0))
		vm_panic("Machine runtime encountered a fatal error\n");

	pr_info("Tearing down machine resources...\n");
	machine_destroy(&mach);
	chardev_stdio_destroy(chr);
}

int main(void)
{
	pr_info("Initiating ModVM Machine Architecture Test\n\n");

	test_machine_lifecycle();

	pr_info("\nMachine architecture test completed gracefully\n");
	return 0;
}