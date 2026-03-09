/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include <modvm/compiler.h>
#include <modvm/build_bug.h>
#include <modvm/stddef.h>
#include <modvm/container_of.h>
#include <modvm/list.h>
#include <modvm/log.h>
#include <modvm/bug.h>

#undef pr_fmt
#define pr_fmt(fmt) "test_infra: " fmt

struct mock_hw_device {
	uint32_t control_reg; /* Offset: 0 */
	uint32_t status_reg; /* Offset: 4 */

	struct_group(dma_regs, uint32_t dma_src; /* Offset: 8 */
		     uint32_t dma_dst; /* Offset: 12 */
	);

	DECLARE_FLEX_ARRAY(uint8_t, fifo_buffer); /* Offset: 16 */
};

ASSERT_STRUCT_OFFSET(struct mock_hw_device, control_reg, 0);
ASSERT_STRUCT_OFFSET(struct mock_hw_device, status_reg, 4);
ASSERT_STRUCT_OFFSET(struct mock_hw_device, dma_src, 8);
ASSERT_STRUCT_OFFSET(struct mock_hw_device, fifo_buffer, 16);

static_assert(__same_type(uint32_t, unsigned int), "Type matching failed");

struct vm_task {
	int vcpu_id;
	const char *task_name;
	struct list_head sched_link;
	struct hlist_node hash_link;
};

static void test_container_of(void)
{
	struct vm_task task = { .vcpu_id = 42 };
	struct list_head *link_ptr = &task.sched_link;

	struct vm_task *recovered =
		container_of(link_ptr, struct vm_task, sched_link);

	if (WARN_ON(recovered->vcpu_id != 42)) {
		vm_panic("container_of macro failed!\n");
	}
	pr_info("container_of() tests passed.\n");
}

static void test_double_linked_list(void)
{
	LIST_HEAD(run_queue);

	struct vm_task t1 = { .vcpu_id = 1, .task_name = "Init" };
	struct vm_task t2 = { .vcpu_id = 2, .task_name = "Network" };
	struct vm_task t3 = { .vcpu_id = 3, .task_name = "BlockIO" };

	list_add_tail(&t1.sched_link, &run_queue);
	list_add_tail(&t2.sched_link, &run_queue);
	list_add_tail(&t3.sched_link, &run_queue);

	if (WARN_ON(list_empty(&run_queue))) {
		vm_panic("List should not be empty!\n");
	}

	struct vm_task *pos, *n;
	int expected_id = 1;

	list_for_each_entry_safe(pos, n, &run_queue, sched_link)
	{
		pr_debug("Scheduling task: %s (vCPU %d)\n", pos->task_name,
			 pos->vcpu_id);

		if (pos->vcpu_id != expected_id++) {
			vm_panic("List order corrupted!\n");
		}

		list_del_init(&pos->sched_link);
	}

	if (WARN_ON(!list_empty(&run_queue))) {
		vm_panic("List deletion failed, not empty!\n");
	}

	pr_info("Double-linked list (list_head) tests passed.\n");
}

static void test_hash_list(void)
{
	HLIST_HEAD(task_hash_bucket);

	struct vm_task t1 = { .vcpu_id = 100 };
	struct vm_task t2 = { .vcpu_id = 200 };

	hlist_add_head(&t1.hash_link, &task_hash_bucket);
	hlist_add_head(&t2.hash_link, &task_hash_bucket);

	int count = 0;
	struct vm_task *pos;
	hlist_for_each_entry(pos, &task_hash_bucket, hash_link)
	{
		count++;
	}

	if (WARN_ON(count != 2)) {
		vm_panic("Hash list count mismatch!\n");
	}

	pr_info("Hash list (hlist) tests passed.\n");
}

static void test_diagnostics(void)
{
	compiletime_assert_atomic_type(long);

	int condition_true_count = 0;
	int once_action_count = 0;

	pr_info("Testing WARN_ON_ONCE (you should only see the warning ONE time):\n");

	for (int i = 0; i < 3; i++) {
		if (WARN_ON_ONCE(i >= 0)) {
			condition_true_count++;
		}

		if (__ONCE_LITE_IF(i >= 0)) {
			once_action_count++;
		}
	}

	if (condition_true_count != 3) {
		vm_panic("WARN_ON_ONCE did not pass through the condition!\n");
	}

	if (once_action_count != 1) {
		vm_panic("__ONCE_LITE_IF executed %d times instead of 1!\n",
			 once_action_count);
	}

	if (likely(condition_true_count == 3 && once_action_count == 1)) {
		pr_info("The ONCE semantics and branch prediction passed.\n");
	}
}

#include <modvm/bus.h>

struct mock_timer_state {
	uint32_t ticks;
	bool irq_enabled;
};

/*
 * mock_timer_read - Ensure the bus is calculating the relative offset
 */
static uint64_t mock_timer_read(struct vm_device *dev, uint64_t offset,
				uint8_t size)
{
	struct mock_timer_state *state = dev->private_data;

	if (WARN_ON(size == 0))
		return 0;

	/* Offset 0 is the data register in our mock device */
	if (offset == 0) {
		return state->ticks++;
	}
	return 0;
}

static void mock_timer_write(struct vm_device *dev, uint64_t offset,
			     uint64_t value, uint8_t size)
{
	struct mock_timer_state *state = dev->private_data;
	(void)size;

	if (offset == 0) {
		state->ticks = (uint32_t)value;
	} else if (offset == 3) {
		state->irq_enabled = (value == 1);
	}
}

static const struct vm_device_ops mock_timer_ops = {
	.read = mock_timer_read,
	.write = mock_timer_write,
};

static void test_bus_routing(void)
{
	pr_info("Testing Bus Region Routing and Isolation...\n");

	struct mock_timer_state timer_state = { .ticks = 100,
						.irq_enabled = false };

	struct vm_device timer_dev = {
		.name = "mock_timer",
		.ops = &mock_timer_ops,
		.private_data = &timer_state,
	};

	/* Register to PIO space at base 0x40 */
	int ret = bus_register_region(VM_BUS_SPACE_PIO, 0x40, 4, &timer_dev);
	if (WARN_ON(ret != 0)) {
		vm_panic("Failed to register PIO device\n");
	}

	/* Verify collision detection within the same address space */
	struct vm_device dummy_dev = { .name = "dummy",
				       .ops = &mock_timer_ops };
	ret = bus_register_region(VM_BUS_SPACE_PIO, 0x42, 1, &dummy_dev);
	if (WARN_ON(ret == 0)) {
		vm_panic("Bus allowed overlapping PIO registration!\n");
	}
	pr_debug("Bus successfully rejected overlapping registration.\n");

	/* Verify space isolation: Same address but in MMIO space should succeed */
	ret = bus_register_region(VM_BUS_SPACE_MMIO, 0x42, 1, &dummy_dev);
	if (WARN_ON(ret != 0)) {
		vm_panic(
			"Bus failed to isolate PIO and MMIO address spaces!\n");
	}
	pr_debug("Bus successfully isolated MMIO from PIO.\n");

	/* Test write dispatch: target absolute address 0x40 */
	bus_dispatch_write(VM_BUS_SPACE_PIO, 0x40, 500, 4);
	if (WARN_ON(timer_state.ticks != 500)) {
		vm_panic("Write routing failed to mutate state\n");
	}

	/* Test read dispatch: target absolute address 0x40 */
	uint64_t val = bus_dispatch_read(VM_BUS_SPACE_PIO, 0x40, 4);
	if (WARN_ON(val != 500)) {
		vm_panic("Read routing returned wrong value: %lu\n", val);
	}
	if (WARN_ON(timer_state.ticks != 501)) {
		vm_panic("Hardware internal state machine failed to tick\n");
	}

	/* Target an unmapped address in PIO space */
	uint64_t unmapped = bus_dispatch_read(VM_BUS_SPACE_PIO, 0x3F8, 1);
	if (WARN_ON(unmapped != ~0ULL)) {
		vm_panic("Unmapped port returned %lx instead of all 1s!\n",
			 unmapped);
	}

	pr_info("Bus routing and isolation tests passed.\n");
}

int main(void)
{
	pr_info("Starting modvm infrastructure sanity checks...\n");

	test_container_of();
	test_double_linked_list();
	test_hash_list();
	test_diagnostics();
	test_bus_routing();

	pr_info("SUCCESS: All core infrastructure tests passed perfectly!\n");

	return 0;
}