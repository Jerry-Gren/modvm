/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/machine.h>
#include <modvm/core/device.h>
#include <modvm/core/devres.h>
#include <modvm/utils/log.h>
#include <modvm/utils/compiler.h>
#include <modvm/core/loader.h>
#include <modvm/hw/serial.h>
#include <modvm/core/irq.h>

#undef pr_fmt
#define pr_fmt(fmt) "pc_board: " fmt

struct pc_irq_route {
	struct vm_hypervisor *hv;
	uint32_t gsi;
};

static void pc_irq_handler(void *data, int level)
{
	struct pc_irq_route *route = data;
	vm_hypervisor_set_irq(route->hv, route->gsi, level);
}

/**
 * pc_board_init - Assemble the legacy x86 personal computer topology.
 * @machine: The machine instance being constructed.
 *
 * Wires up legacy components such as the serial port at PIO 0x3f8.
 *
 * Return: 0 upon successful assembly, or a negative error code.
 */
static int pc_board_init(struct vm_machine *machine)
{
	struct vm_device *uart;
	struct vm_device *exit_dev;
	struct serial_pdata uart_pdata;
	struct pc_irq_route *uart_route;
	int ret;

	ret = vm_hypervisor_setup_irqchip(&machine->hv);
	if (ret < 0)
		return ret;

	uart = vm_device_alloc(machine, "uart-16550a");
	if (!uart)
		return -ENOMEM;

	uart_route = vm_devm_zalloc(uart, sizeof(*uart_route));
	if (!uart_route) {
		ret = -ENOMEM;
		goto err_uart;
	}

	uart_route->hv = &machine->hv;
	uart_route->gsi = 4;

	uart_pdata.base = 0x3f8;
	uart_pdata.console = machine->config.console;
	uart_pdata.irq = vm_devm_irq_alloc(uart, pc_irq_handler, uart_route);
	if (!uart_pdata.irq) {
		ret = -ENOMEM;
		goto err_uart;
	}

	ret = vm_device_add(uart, &uart_pdata);
	if (ret < 0)
		goto err_uart;

	exit_dev = vm_device_alloc(machine, "debug-exit");
	if (!exit_dev)
		return -ENOMEM;

	ret = vm_device_add(exit_dev, NULL);
	if (ret < 0) {
		vm_device_put(exit_dev);
		return ret;
	}

	return 0;

err_uart:
	vm_device_put(uart);
	return ret;
}

/**
 * pc_board_reset - Orchestrate the boot sequence for the PC architecture.
 * @machine: The machine instance to reset.
 *
 * Loads the firmware payload into memory and points the bootstrap
 * processor to the reset vector.
 *
 * Return: 0 on success, or a negative error code.
 */
static int pc_board_reset(struct vm_machine *machine)
{
	uint64_t boot_pc = 0x0000;
	int ret;

	if (machine->config.firmware_path) {
		ret = vm_loader_load_raw(&machine->hv.mem_space,
					 machine->config.firmware_path,
					 boot_pc);
		if (ret < 0) {
			pr_err("failed to map firmware payload: %s\n",
			       machine->config.firmware_path);
			return ret;
		}
	}

	ret = vm_vcpu_set_pc(machine->vcpus[0], boot_pc);
	if (ret < 0) {
		pr_err("failed to configure bootstrap processor pc\n");
		return ret;
	}

	return 0;
}

static const struct vm_machine_ops pc_ops = {
	.init = pc_board_init,
	.reset = pc_board_reset,
};

static const struct vm_machine_class pc_class = {
	.name = "pc",
	.desc = "Standard x86 Personal Computer",
	.ops = &pc_ops,
};

static void __attribute__((constructor)) register_pc_class(void)
{
	vm_machine_class_register(&pc_class);
}