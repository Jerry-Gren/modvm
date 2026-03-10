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

struct mock_dev {
	uint32_t ctrl_reg;
	uint32_t stat_reg;

	struct_group(dma_regs, uint32_t dma_src; uint32_t dma_dst;);

	DECLARE_FLEX_ARRAY(uint8_t, fifo);
};

ASSERT_STRUCT_OFFSET(struct mock_dev, ctrl_reg, 0);
ASSERT_STRUCT_OFFSET(struct mock_dev, stat_reg, 4);
ASSERT_STRUCT_OFFSET(struct mock_dev, dma_src, 8);
ASSERT_STRUCT_OFFSET(struct mock_dev, fifo, 16);

static_assert(__same_type(uint32_t, unsigned int), "type matching failed");

struct vm_task {
	int id;
	const char *name;
	struct list_head node;
	struct hlist_node hnode;
};

static void test_container_of(void)
{
	struct vm_task parent = { .id = 42 };
	struct list_head *link = &parent.node;

	struct vm_task *recovered = container_of(link, struct vm_task, node);

	if (WARN_ON(recovered->id != 42))
		vm_panic("container_of macro mapping failed\n");

	pr_info("container_of structural translation passed\n");
}

static void test_list(void)
{
	LIST_HEAD(run_queue);

	struct vm_task t1 = { .id = 1, .name = "init" };
	struct vm_task t2 = { .id = 2, .name = "network" };
	struct vm_task t3 = { .id = 3, .name = "block_io" };

	list_add_tail(&t1.node, &run_queue);
	list_add_tail(&t2.node, &run_queue);
	list_add_tail(&t3.node, &run_queue);

	if (WARN_ON(list_empty(&run_queue)))
		vm_panic("queue should not be empty after insertions\n");

	struct vm_task *pos, *n;
	int expected_id = 1;

	list_for_each_entry_safe(pos, n, &run_queue, node)
	{
		pr_debug("scheduling task: %s (id %d)\n", pos->name, pos->id);

		if (pos->id != expected_id++)
			vm_panic(
				"linked list topological ordering corrupted\n");

		list_del_init(&pos->node);
	}

	if (WARN_ON(!list_empty(&run_queue)))
		vm_panic("list node deletion failed, artifacts remain\n");

	pr_info("doubly-linked list memory operations passed\n");
}

struct mock_timer_ctx {
	uint32_t ticks;
	bool irq_en;
};

static uint64_t mock_timer_read(struct vm_device *dev, uint64_t offset,
				uint8_t size)
{
	struct mock_timer_ctx *ctx = dev->priv;

	if (WARN_ON(size == 0))
		return 0;

	if (offset == 0)
		return ctx->ticks++;

	return 0;
}

static void mock_timer_write(struct vm_device *dev, uint64_t offset,
			     uint64_t val, uint8_t size)
{
	struct mock_timer_ctx *ctx = dev->priv;
	(void)size;

	if (offset == 0)
		ctx->ticks = (uint32_t)val;
	else if (offset == 3)
		ctx->irq_en = (val == 1);
}

static const struct vm_device_ops mock_timer_ops = {
	.read = mock_timer_read,
	.write = mock_timer_write,
};

static void test_bus_routing(void)
{
	struct mock_timer_ctx timer_ctx = { .ticks = 100, .irq_en = false };
	struct vm_device timer_dev = {
		.name = "mock_timer",
		.ops = &mock_timer_ops,
		.priv = &timer_ctx,
	};
	struct vm_device dummy_dev = {
		.name = "dummy",
		.ops = &mock_timer_ops,
	};

	int ret;
	uint64_t val;

	pr_info("evaluating system bus topological routing\n");

	ret = vm_bus_register_region(VM_BUS_PIO, 0x40, 4, &timer_dev);
	if (WARN_ON(ret != 0))
		vm_panic("failed to register pio peripheral\n");

	ret = vm_bus_register_region(VM_BUS_PIO, 0x42, 1, &dummy_dev);
	if (WARN_ON(ret == 0))
		vm_panic("bus permitted overlapping address registration\n");

	ret = vm_bus_register_region(VM_BUS_MMIO, 0x42, 1, &dummy_dev);
	if (WARN_ON(ret != 0))
		vm_panic("bus failed to isolate pio and mmio spaces\n");

	vm_bus_dispatch_write(VM_BUS_PIO, 0x40, 500, 4);
	if (WARN_ON(timer_ctx.ticks != 500))
		vm_panic("write routing failed to mutate state\n");

	val = vm_bus_dispatch_read(VM_BUS_PIO, 0x40, 4);
	if (WARN_ON(val != 500))
		vm_panic("read routing returned incorrect data: %lu\n", val);

	if (WARN_ON(timer_ctx.ticks != 501))
		vm_panic("hardware state machine failed to tick upon read\n");

	val = vm_bus_dispatch_read(VM_BUS_PIO, 0x3f8, 1);
	if (WARN_ON(val != ~0ULL))
		vm_panic("unmapped port returned %lx instead of high state\n",
			 val);

	pr_info("bus routing and topology isolation passed\n");
}

int main(void)
{
	pr_info("initiating modvm infrastructure diagnostic sequence\n");

	test_container_of();
	test_list();
	test_bus_routing();

	pr_info("SUCCESS: all core infrastructure checks completed\n");
	return 0;
}