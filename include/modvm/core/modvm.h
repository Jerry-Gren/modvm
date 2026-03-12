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
 * struct modvm_config - user supplied virtual machine parameters
 * @accel_name: string identifier of the requested hypervisor backend
 * @ram_base: guest physical address where primary memory starts
 * @ram_size: total guest physical memory in bytes
 * @nr_vcpus: number of virtual processors to instantiate
 * @firmware_path: host filesystem path to the guest firmware
 * @board: pointer to the selected motherboard blueprint
 * @console: host character device mapped to the primary console
 */
struct modvm_config {
	const char *accel_name;
	uint64_t ram_base;
	size_t ram_size;
	unsigned int nr_vcpus;
	const char *firmware_path;
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