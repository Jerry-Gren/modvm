/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <modvm/core/machine.h>
#include <modvm/core/device.h>
#include <modvm/core/devres.h>
#include <modvm/core/memory.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>
#include <modvm/core/chardev.h>
#include <modvm/core/irq.h>
#include <modvm/hw/serial.h>
#include <modvm/backends/char/stdio.h>

#undef pr_fmt
#define pr_fmt(fmt) "test_machine: " fmt

static const uint8_t fw_payload[] = { 0xba, 0xf8, 0x03, 0xb0, 0x4f, 0xee,
				      0xb0, 0x4b, 0xee, 0xb0, 0x0d, 0xee,
				      0xb0, 0x0a, 0xee, 0xba, 0x00, 0x05,
				      0xb0, 0x01, 0xee, 0xf4 };

struct mock_irq_route {
	struct vm_hypervisor *hv;
	uint32_t gsi;
};

static void mock_irq_handler(void *data, int level)
{
	struct mock_irq_route *route = data;
	vm_hypervisor_set_irq(route->hv, route->gsi, level);
}

static int mock_machine_init(struct vm_machine *machine)
{
	struct vm_device *uart;
	struct vm_device *exit_dev;
	struct serial_pdata pdata;
	struct mock_irq_route *route;
	int ret;

	ret = vm_hypervisor_setup_irqchip(&machine->hv);
	if (ret < 0) {
		pr_err("failed to initialize architectural irqchip\n");
		return ret;
	}

	/* 替换掉废弃的 vm_device_create，拥抱两段式构建 */
	uart = vm_device_alloc(machine, "uart-16550a");
	if (!uart)
		return -ENOMEM;

	route = vm_devm_zalloc(uart, sizeof(*route));
	if (!route) {
		ret = -ENOMEM;
		goto err_uart;
	}

	route->hv = &machine->hv;
	route->gsi = 4;

	pdata.base = 0x3f8;
	pdata.console = machine->config.console;
	pdata.irq = vm_devm_irq_alloc(uart, mock_irq_handler, route);
	if (!pdata.irq) {
		ret = -ENOMEM;
		goto err_uart;
	}

	ret = vm_device_add(uart, &pdata);
	if (ret < 0) {
		pr_err("failed to probe uart peripheral\n");
		goto err_uart;
	}

	exit_dev = vm_device_alloc(machine, "debug-exit");
	if (!exit_dev)
		return -ENOMEM;

	ret = vm_device_add(exit_dev, NULL);
	if (ret < 0) {
		pr_err("failed to probe debug exit device\n");
		vm_device_put(exit_dev);
		return ret;
	}

	pr_info("mock hardware topology injected successfully\n");
	return 0;

err_uart:
	vm_device_put(uart);
	return ret;
}

static int mock_machine_reset(struct vm_machine *machine)
{
	void *hva;
	int ret;

	hva = vm_mem_gpa_to_hva(&machine->hv.mem_space, 0x0000);
	if (IS_ERR_OR_NULL(hva)) {
		pr_err("failed to translate gpa 0x0000 for payload injection\n");
		return -EFAULT;
	}

	memcpy(hva, fw_payload, sizeof(fw_payload));
	pr_info("injected %zu bytes of machine code into guest memory\n",
		sizeof(fw_payload));

	ret = vm_vcpu_set_pc(machine->vcpus[0], 0x0000);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct vm_machine_ops mock_ops = {
	.init = mock_machine_init,
	.reset = mock_machine_reset,
};

static const struct vm_machine_class mock_class = {
	.name = "mock",
	.desc = "Mock machine for integration testing",
	.ops = &mock_ops,
};

static void test_machine_lifecycle(void)
{
	struct vm_machine vm;
	struct vm_chardev *console;
	int ret;

	console = vm_chardev_stdio_create();
	if (WARN_ON(!console))
		vm_panic("failed to create console backend\n");

	struct vm_machine_config cfg = {
		.accel_name = "kvm",
		.ram_base = 0x0000,
		.ram_size = 4096,
		.nr_vcpus = 1,
		.firmware_path = NULL,
		.machine_class = &mock_class,
		.console = console,
	};

	pr_info("initiating motherboard initialization sequence\n");

	ret = vm_machine_init(&vm, &cfg);
	if (WARN_ON(ret < 0))
		vm_panic("machine assembly failed\n");

	pr_info("igniting processor cores\n");

	ret = vm_machine_run(&vm);
	if (WARN_ON(ret < 0))
		vm_panic("hypervisor runtime encountered fatal error\n");

	pr_info("tearing down virtualization context\n");
	vm_machine_destroy(&vm);
	vm_chardev_stdio_destroy(console);
}

int main(void)
{
	pr_info("Initiating ModVM machine integration test\n");
	test_machine_lifecycle();
	pr_info("SUCCESS: machine architecture test concluded gracefully\n");
	return 0;
}