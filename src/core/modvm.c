/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/modvm.h>
#include <modvm/core/board.h>
#include <modvm/core/device.h>
#include <modvm/core/ctxm.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>
#include <modvm/os/event_loop.h>

#undef pr_fmt
#define pr_fmt(fmt) "modvm: " fmt

struct vcpu_run_data {
	struct modvm_ctx *ctx;
	struct modvm_vcpu *vcpu;
};

static void *vcpu_thread_fn(void *data)
{
	struct vcpu_run_data *run_data = data;
	int ret;

	ret = modvm_vcpu_run(run_data->vcpu);
	if (ret < 0) {
		pr_err("fatal execution error on vcpu %d, initiating emergency shutdown\n",
		       run_data->vcpu->id);
		/* This will notify other vcpus */
		modvm_request_shutdown(run_data->ctx);
	}

	return NULL;
}

/**
 * modvm_init - assemble the virtual machine topology and core runtime
 * @ctx: the context object to initialize
 * @config: the immutable configuration parameters
 *
 * Allocates processors, initializes the core acceleration context,
 * and invokes the board-specific initialization hook to wire up RAM
 * and peripherals.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int modvm_init(struct modvm_ctx *ctx, const struct modvm_config *config)
{
	unsigned int i;
	int ret;

	if (WARN_ON(!ctx || !config))
		return -EINVAL;

	ctx->config = *config;

	INIT_LIST_HEAD(&ctx->devices);
	INIT_LIST_HEAD(&ctx->bus.pio_regions);
	INIT_LIST_HEAD(&ctx->bus.mmio_regions);
	modvm_res_pool_init(&ctx->ctxm_pool, ctx);

	ret = modvm_event_loop_init(ctx);
	if (ret < 0) {
		pr_err("failed to initialize private asynchronous event loop\n");
		return ret;
	}

	ret = modvm_accel_init(&ctx->accel, ctx->config.accel_name, &ctx->bus);
	if (ret < 0) {
		pr_err("failed to instantiate hardware acceleration engine\n");
		return ret;
	}

	ret = modvm_ctxm_add_action(ctx, modvm_accel_destroy, &ctx->accel);
	if (ret < 0) {
		modvm_accel_destroy(&ctx->accel);
		return ret;
	}

	/*
	 * We delegate physical memory mapping to the specific board class,
	 * allowing it to handle architectural quirks like the x86 PCI hole.
	 */
	if (ctx->config.board && ctx->config.board->ops &&
	    ctx->config.board->ops->init) {
		ret = ctx->config.board->ops->init(ctx);
		if (ret < 0) {
			pr_err("board physical topology wiring failed\n");
			return ret;
		}
	}

	ctx->vcpus = modvm_ctxm_zalloc(
		ctx, ctx->config.nr_vcpus * sizeof(struct modvm_vcpu *));
	ctx->vcpu_threads = modvm_ctxm_zalloc(
		ctx, ctx->config.nr_vcpus * sizeof(struct os_thread *));
	if (!ctx->vcpus || !ctx->vcpu_threads)
		return -ENOMEM;

	for (i = 0; i < ctx->config.nr_vcpus; i++) {
		ctx->vcpus[i] =
			modvm_ctxm_zalloc(ctx, sizeof(struct modvm_vcpu));
		if (!ctx->vcpus[i])
			return -ENOMEM;

		ret = modvm_vcpu_init(ctx->vcpus[i], &ctx->accel, i);
		if (ret < 0) {
			pr_err("failed to instantiate vcpu %u\n", i);
			return ret;
		}

		ret = modvm_ctxm_add_action(ctx, modvm_vcpu_destroy,
					    ctx->vcpus[i]);
		if (ret < 0) {
			modvm_vcpu_destroy(ctx->vcpus[i]);
			return ret;
		}
	}

	pr_info("context assembled with %zu bytes ram and %u vcpus\n",
		ctx->config.ram_size, ctx->config.nr_vcpus);

	return 0;
}

/**
 * modvm_run - ignite the processor cores and enter the main dispatch loop
 * @ctx: the fully initialized context object
 *
 * Return: 0 upon successful shutdown, or a negative error code.
 */
int modvm_run(struct modvm_ctx *ctx)
{
	struct vcpu_run_data *run_data;
	unsigned int i;
	int ret;

	if (WARN_ON(!ctx))
		return -EINVAL;

	pr_info("powering on the virtual machine context...\n");

	os_thread_system_init();
	atomic_store(&ctx->accel.is_running, true);

	if (ctx->config.board && ctx->config.board->ops &&
	    ctx->config.board->ops->reset) {
		ret = ctx->config.board->ops->reset(ctx);
		if (ret < 0) {
			pr_err("board reset protocol and firmware injection failed\n");
			return ret;
		}
	}

	run_data = modvm_ctxm_zalloc(ctx,
				     ctx->config.nr_vcpus * sizeof(*run_data));
	if (!run_data)
		return -ENOMEM;

	os_mutex_lock(ctx->accel.init_mutex);

	for (i = 0; i < ctx->config.nr_vcpus; i++) {
		run_data[i].ctx = ctx;
		run_data[i].vcpu = ctx->vcpus[i];

		ctx->vcpu_threads[i] =
			os_thread_create(vcpu_thread_fn, &run_data[i]);

		if (IS_ERR(ctx->vcpu_threads[i])) {
			pr_err("failed to spawn kernel thread for vcpu %u\n",
			       i);
			ctx->vcpu_threads[i] = NULL;
			continue;
		}

		modvm_ctxm_add_action(ctx, os_thread_destroy,
				      ctx->vcpu_threads[i]);
	}

	os_mutex_unlock(ctx->accel.init_mutex);

	modvm_event_loop_run(ctx);

	for (i = 0; i < ctx->config.nr_vcpus; i++) {
		if (ctx->vcpu_threads[i])
			os_thread_join(ctx->vcpu_threads[i]);
	}

	pr_info("context powered off successfully\n");
	return 0;
}

/**
 * modvm_request_shutdown - broadcast a termination signal to all cores
 * @ctx: the active context object
 */
void modvm_request_shutdown(struct modvm_ctx *ctx)
{
	unsigned int i;

	if (WARN_ON(!ctx))
		return;

	atomic_store(&ctx->accel.is_running, false);

	for (i = 0; i < ctx->config.nr_vcpus; i++) {
		if (ctx->vcpu_threads[i])
			os_thread_send_wakeup(ctx->vcpu_threads[i]);
	}

	modvm_event_loop_stop(ctx);
}

/**
 * modvm_destroy - tear down topology and securely release all host resources
 * @ctx: the context to destroy
 */
void modvm_destroy(struct modvm_ctx *ctx)
{
	struct modvm_device *dev;
	struct modvm_device *n;

	if (WARN_ON(!ctx))
		return;

	/*
	 * Outer perimeter teardown: Dismantle peripheral devices first so they
	 * unregister from buses before memory maps disappear.
	 */
	list_for_each_entry_safe(dev, n, &ctx->devices, node)
	{
		list_del(&dev->node);

		if (dev->ops && dev->ops->unrealize)
			dev->ops->unrealize(dev);

		modvm_device_put(dev);
	}

	/*
	 * Core perimeter teardown: Recursively frees threads, vcpus, event loop,
	 * and finally the hypervisor context in reverse order of allocation.
	 */
	modvm_res_release_all(&ctx->ctxm_pool);
}