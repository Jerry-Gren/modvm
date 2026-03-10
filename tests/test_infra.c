/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include <modvm/utils/compiler.h>
#include <modvm/utils/build_bug.h>
#include <modvm/utils/stddef.h>
#include <modvm/utils/container_of.h>
#include <modvm/utils/list.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>

#include <modvm/core/bus.h>

#undef pr_fmt
#define pr_fmt(fmt) "test_infra: " fmt

struct mock_hardware_device {
	uint32_t control_register;
	uint32_t status_register;

	struct_group(dma_registers, uint32_t dma_source;
		     uint32_t dma_destination;);

	DECLARE_FLEX_ARRAY(uint8_t, fifo_buffer);
};

ASSERT_STRUCT_OFFSET(struct mock_hardware_device, control_register, 0);
ASSERT_STRUCT_OFFSET(struct mock_hardware_device, status_register, 4);
ASSERT_STRUCT_OFFSET(struct mock_hardware_device, dma_source, 8);
ASSERT_STRUCT_OFFSET(struct mock_hardware_device, fifo_buffer, 16);

static_assert(__same_type(uint32_t, unsigned int), "type matching failed");

struct virtual_machine_task {
	int processor_id;
	const char *task_name;
	struct list_head scheduler_link;
	struct hlist_node hash_table_link;
};

static void evaluate_container_of_macro(void)
{
	struct virtual_machine_task parent_task = { .processor_id = 42 };
	struct list_head *link_pointer = &parent_task.scheduler_link;

	struct virtual_machine_task *recovered_task = container_of(
		link_pointer, struct virtual_machine_task, scheduler_link);

	if (WARN_ON(recovered_task->processor_id != 42)) {
		vm_panic("container_of macro mapping failed\n");
	}
	pr_info("container_of structural translation passed\n");
}

static void evaluate_doubly_linked_list(void)
{
	LIST_HEAD(run_queue);

	struct virtual_machine_task task_one = { .processor_id = 1,
						 .task_name = "init" };
	struct virtual_machine_task task_two = { .processor_id = 2,
						 .task_name = "network" };
	struct virtual_machine_task task_three = { .processor_id = 3,
						   .task_name = "block_io" };

	list_add_tail(&task_one.scheduler_link, &run_queue);
	list_add_tail(&task_two.scheduler_link, &run_queue);
	list_add_tail(&task_three.scheduler_link, &run_queue);

	if (WARN_ON(list_empty(&run_queue))) {
		vm_panic("queue should not be empty after insertions\n");
	}

	struct virtual_machine_task *current_task;
	struct virtual_machine_task *next_task;
	int expected_identifier = 1;

	list_for_each_entry_safe(current_task, next_task, &run_queue,
				 scheduler_link)
	{
		pr_debug("scheduling task: %s (processor %d)\n",
			 current_task->task_name, current_task->processor_id);

		if (current_task->processor_id != expected_identifier++) {
			vm_panic(
				"linked list topological ordering corrupted\n");
		}

		list_del_init(&current_task->scheduler_link);
	}

	if (WARN_ON(!list_empty(&run_queue))) {
		vm_panic(
			"list node deletion failed, architectural artifacts remain\n");
	}

	pr_info("doubly-linked list memory operations passed\n");
}

struct mock_timer_state {
	uint32_t execution_ticks;
	bool interrupt_enabled;
};

static uint64_t mock_timer_hardware_read(struct vm_device *device,
					 uint64_t register_offset,
					 uint8_t access_size)
{
	struct mock_timer_state *state = device->private_data;

	if (WARN_ON(access_size == 0))
		return 0;

	if (register_offset == 0) {
		return state->execution_ticks++;
	}
	return 0;
}

static void mock_timer_hardware_write(struct vm_device *device,
				      uint64_t register_offset,
				      uint64_t payload, uint8_t access_size)
{
	struct mock_timer_state *state = device->private_data;
	(void)access_size;

	if (register_offset == 0) {
		state->execution_ticks = (uint32_t)payload;
	} else if (register_offset == 3) {
		state->interrupt_enabled = (payload == 1);
	}
}

static const struct vm_device_operations mock_timer_operations = {
	.read = mock_timer_hardware_read,
	.write = mock_timer_hardware_write,
};

static void evaluate_system_bus_routing(void)
{
	struct mock_timer_state timer_internal_state = {
		.execution_ticks = 100, .interrupt_enabled = false
	};
	struct vm_device timer_peripheral = {
		.name = "mock_timer",
		.operations = &mock_timer_operations,
		.private_data = &timer_internal_state,
	};
	struct vm_device dummy_peripheral = {
		.name = "dummy",
		.operations = &mock_timer_operations,
	};

	int return_code;
	uint64_t read_value;
	uint64_t unmapped_value;

	pr_info("evaluating system bus topological routing and isolation\n");

	return_code = vm_bus_register_region(VM_BUS_SPACE_PORT_IO, 0x40, 4,
					     &timer_peripheral);
	if (WARN_ON(return_code != 0)) {
		vm_panic("failed to register port io peripheral\n");
	}

	return_code = vm_bus_register_region(VM_BUS_SPACE_PORT_IO, 0x42, 1,
					     &dummy_peripheral);
	if (WARN_ON(return_code == 0)) {
		vm_panic("bus permitted overlapping address registration\n");
	}

	return_code = vm_bus_register_region(VM_BUS_SPACE_MEMORY_MAPPED_IO,
					     0x42, 1, &dummy_peripheral);
	if (WARN_ON(return_code != 0)) {
		vm_panic(
			"bus failed to isolate port io and memory mapped io spaces\n");
	}

	vm_bus_dispatch_write(VM_BUS_SPACE_PORT_IO, 0x40, 500, 4);
	if (WARN_ON(timer_internal_state.execution_ticks != 500)) {
		vm_panic(
			"write routing failed to mutate peripheral hardware state\n");
	}

	read_value = vm_bus_dispatch_read(VM_BUS_SPACE_PORT_IO, 0x40, 4);
	if (WARN_ON(read_value != 500)) {
		vm_panic("read routing returned incorrect payload data: %lu\n",
			 read_value);
	}

	if (WARN_ON(timer_internal_state.execution_ticks != 501)) {
		vm_panic(
			"hardware internal state machine failed to tick upon read\n");
	}

	unmapped_value = vm_bus_dispatch_read(VM_BUS_SPACE_PORT_IO, 0x3F8, 1);
	if (WARN_ON(unmapped_value != ~0ULL)) {
		vm_panic(
			"unmapped port returned %lx instead of floating high state\n",
			unmapped_value);
	}

	pr_info("bus routing and topology isolation passed\n");
}

int main(void)
{
	pr_info("initiating modvm infrastructure diagnostic sequence\n");

	evaluate_container_of_macro();
	evaluate_doubly_linked_list();
	evaluate_system_bus_routing();

	pr_info("SUCCESS: all core infrastructure checks completed\n");
	return 0;
}