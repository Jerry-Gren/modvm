/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <modvm/utils/compiler.h>
#include <modvm/utils/build_bug.h>
#include <modvm/utils/stddef.h>
#include <modvm/utils/container_of.h>
#include <modvm/utils/list.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/core/res_pool.h>

#include <modvm/core/bus.h>
#include <modvm/core/modvm.h>

#undef pr_fmt
#define pr_fmt(fmt) "test_infra: " fmt

struct mock_hw_regs {
	uint32_t ctrl_reg;
	uint32_t stat_reg;
	struct_group(dma_regs, uint32_t dma_src; uint32_t dma_dst;);
	DECLARE_FLEX_ARRAY(uint8_t, fifo);
};

ASSERT_STRUCT_OFFSET(struct mock_hw_regs, ctrl_reg, 0);
ASSERT_STRUCT_OFFSET(struct mock_hw_regs, stat_reg, 4);
ASSERT_STRUCT_OFFSET(struct mock_hw_regs, dma_src, 8);
ASSERT_STRUCT_OFFSET(struct mock_hw_regs, fifo, 16);

static_assert(__same_type(uint32_t, unsigned int), "type matching failed");

struct mock_task {
	int id;
	const char *name;
	struct list_head node;
};

static void test_container_of(void)
{
	struct mock_task parent = { .id = 42 };
	struct list_head *link = &parent.node;

	struct mock_task *recovered =
		container_of(link, struct mock_task, node);

	if (WARN_ON(recovered->id != 42))
		modvm_panic(
			"container_of macro structural translation failed\n");

	pr_info("container_of mapping passed\n");
}

static void test_list(void)
{
	LIST_HEAD(run_queue);

	struct mock_task t1 = { .id = 1, .name = "init" };
	struct mock_task t2 = { .id = 2, .name = "network" };
	struct mock_task t3 = { .id = 3, .name = "block_io" };

	list_add_tail(&t1.node, &run_queue);
	list_add_tail(&t2.node, &run_queue);
	list_add_tail(&t3.node, &run_queue);

	if (WARN_ON(list_empty(&run_queue)))
		modvm_panic(
			"queue falsely reported as empty after insertions\n");

	struct mock_task *pos, *n;
	int expected_id = 1;

	list_for_each_entry_safe(pos, n, &run_queue, node)
	{
		pr_debug("scheduling task: %s (id %d)\n", pos->name, pos->id);

		if (pos->id != expected_id++)
			modvm_panic(
				"linked list topological ordering corrupted\n");

		list_del_init(&pos->node);
	}

	if (WARN_ON(!list_empty(&run_queue)))
		modvm_panic("list node deletion failed, artifacts remain\n");

	pr_info("doubly-linked list operations passed\n");
}

struct mock_timer_ctx {
	uint32_t ticks;
};

static uint64_t mock_timer_read(struct modvm_device *dev, uint64_t offset,
				uint8_t size)
{
	struct mock_timer_ctx *ctx = dev->priv;

	if (WARN_ON(size == 0))
		return 0;

	if (offset == 0)
		return ctx->ticks++;

	return 0;
}

static void mock_timer_write(struct modvm_device *dev, uint64_t offset,
			     uint64_t val, uint8_t size)
{
	struct mock_timer_ctx *ctx = dev->priv;
	(void)size;

	if (offset == 0)
		ctx->ticks = (uint32_t)val;
}

static const struct modvm_device_ops mock_timer_ops = {
	.read = mock_timer_read,
	.write = mock_timer_write,
};

static void test_bus_routing(void)
{
	struct modvm_ctx mock_ctx;
	struct mock_timer_ctx timer_ctx = { .ticks = 100 };
	struct modvm_device timer_dev = {
		.name = "mock_timer",
		.ops = &mock_timer_ops,
		.priv = &timer_ctx,
		.ctx = &mock_ctx,
	};
	struct modvm_device dummy_dev = {
		.name = "dummy",
		.ops = &mock_timer_ops,
		.ctx = &mock_ctx,
	};
	int ret;
	uint64_t val;

	pr_info("evaluating system bus topological routing\n");

	memset(&mock_ctx, 0, sizeof(mock_ctx));
	INIT_LIST_HEAD(&mock_ctx.bus.pio_regions);
	INIT_LIST_HEAD(&mock_ctx.bus.mmio_regions);

	modvm_res_pool_init(&timer_dev.devm_pool, &timer_dev);
	modvm_res_pool_init(&dummy_dev.devm_pool, &dummy_dev);

	ret = modvm_bus_register_region(MODVM_BUS_PIO, 0x40, 4, &timer_dev);
	if (WARN_ON(ret != 0))
		modvm_panic("failed to register pio peripheral\n");

	ret = modvm_bus_register_region(MODVM_BUS_PIO, 0x42, 1, &dummy_dev);
	if (WARN_ON(ret == 0))
		modvm_panic("bus permitted overlapping address registration\n");

	ret = modvm_bus_register_region(MODVM_BUS_MMIO, 0x42, 1, &dummy_dev);
	if (WARN_ON(ret != 0))
		modvm_panic("bus failed to isolate pio and mmio spaces\n");

	modvm_bus_dispatch_write(&mock_ctx.bus, MODVM_BUS_PIO, 0x40, 500, 4);
	if (WARN_ON(timer_ctx.ticks != 500))
		modvm_panic("write routing failed to mutate state\n");

	val = modvm_bus_dispatch_read(&mock_ctx.bus, MODVM_BUS_PIO, 0x40, 4);
	if (WARN_ON(val != 500))
		modvm_panic("read routing returned incorrect data\n");

	val = modvm_bus_dispatch_read(&mock_ctx.bus, MODVM_BUS_PIO, 0x3f8, 1);
	if (WARN_ON(val != ~0ULL))
		modvm_panic("unmapped port failed floating bus constraint\n");

	modvm_res_release_all(&timer_dev.devm_pool);
	modvm_res_release_all(&dummy_dev.devm_pool);

	pr_info("bus routing and topology isolation passed\n");
}

int main(void)
{
	modvm_log_initialize();
	pr_info("initiating modvm infrastructure diagnostic sequence\n");

	test_container_of();
	test_list();
	test_bus_routing();

	pr_info("SUCCESS: all core infrastructure checks completed\n");
	modvm_log_destroy();
	return 0;
}