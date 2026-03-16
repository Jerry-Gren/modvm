/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_MODVM_H
#define MODVM_CORE_MODVM_H

#include <stdint.h>

#include <modvm/core/res_pool.h>
#include <modvm/core/accel.h>
#include <modvm/core/vcpu.h>
#include <modvm/os/thread.h>
#include <modvm/core/chardev.h>
#include <modvm/utils/list.h>

struct modvm_board;
struct modvm_ctx;

/**
 * struct modvm_bus - encapsulates the system's address space topologies
 * @pio_regions: list of mapped port I/O regions
 * @mmio_regions: list of mapped memory-mapped I/O regions
 */
struct modvm_bus {
	struct list_head pio_regions;
	struct list_head mmio_regions;
};

/**
 * struct modvm_event_loop - context-specific asynchronous dispatch loop
 * @priv: opaque pointer to the host OS-specific event structure
 */
struct modvm_event_loop {
	void *priv;
};

/**
 * struct modvm_config - immutable global configuration for the virtual machine
 * @accel_name: hardware acceleration backend requested
 * @ram_base: starting physical address of system memory
 * @ram_size: total capacity of system memory in bytes
 * @nr_vcpus: number of virtual processors to allocate
 * @loader_name: boot protocol plugin identifier (e.g., "linux-x86", "raw-x86")
 * @loader_opts: protocol-specific configuration string (e.g., "kernel=/boot/vmlinuz")
 * @board_opts: ?
 * @board: selected motherboard topology blueprint
 * @console: character device backend for the primary system console
 */
struct modvm_config {
	const char *accel_name;
	uint64_t ram_base;
	size_t ram_size;
	unsigned int nr_vcpus;
	const char *loader_name;
	const char *loader_opts;
	const char *board_opts;
	const struct modvm_board *board;
	struct modvm_chardev *console;
};

/**
 * struct modvm_ctx - the isolated virtual machine context
 * @config: immutable configuration for this session
 * @ctxm_pool: resource pool for context-level automated teardown
 * @accel: hardware acceleration engine container
 * @bus: memory and port I/O routing topologies
 * @event_loop: asynchronous I/O dispatcher
 * @vcpus: array of virtual processor instances
 * @vcpu_threads: array of host OS threads driving the processors
 * @devices: topological registry of all instantiated peripherals
 *
 * An opaque-like container holding the entire state of a single VM instance.
 * It is safe to instantiate multiple contexts within the same host process.
 */
struct modvm_ctx {
	struct modvm_config config;
	struct modvm_res_pool ctxm_pool;
	struct modvm_accel accel;
	struct modvm_bus bus;
	struct modvm_event_loop event_loop;
	struct modvm_vcpu **vcpus;
	struct os_thread **vcpu_threads;
	struct list_head devices;
};

int modvm_init(struct modvm_ctx *ctx, const struct modvm_config *config);
int modvm_run(struct modvm_ctx *ctx);
void modvm_request_shutdown(struct modvm_ctx *ctx);
void modvm_destroy(struct modvm_ctx *ctx);

#endif /* MODVM_CORE_MODVM_H */