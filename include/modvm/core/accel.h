/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_ACCEL_H
#define MODVM_CORE_ACCEL_H

#include <stdatomic.h>
#include <modvm/core/memory.h>
#include <modvm/os/thread.h>

struct modvm_accel;
struct modvm_vcpu_ops;

/**
 * struct modvm_accel_ops - hardware accelerator backend operations
 * @init: initialize the hardware acceleration context
 * @destroy: release hardware acceleration resources
 * @setup_irqchip: synthesize the architectural interrupt controller
 * @set_irq: assert or deassert a specific hardware interrupt line
 */
struct modvm_accel_ops {
	int (*init)(struct modvm_accel *accel);
	void (*destroy)(struct modvm_accel *accel);
	int (*setup_irqchip)(struct modvm_accel *accel);
	int (*set_irq)(struct modvm_accel *accel, uint32_t gsi, int level);
};

/**
 * struct modvm_accel_backend - blueprint for an acceleration backend
 * @name: the unique string identifier (e.g., "kvm")
 * @ops: pointer to the backend operations table
 * @vcpu_ops: virtual processor operations associated with this backend
 */
struct modvm_accel_backend {
	const char *name;
	const struct modvm_accel_ops *ops;
	const struct modvm_vcpu_ops *vcpu_ops;
};

/**
 * struct modvm_accel - the virtualization engine context
 * @backend: the blueprint of the selected acceleration backend
 * @mem_space: physical memory controller managing guest mappings
 * @priv: opaque pointer to the underlying accelerator state
 * @is_running: thread-safe power state monitored by all executing processors
 * @init_mutex: synchronization lock for virtual processor startup phase
 */
struct modvm_accel {
	const struct modvm_accel_backend *backend;
	struct modvm_mem_space mem_space;
	void *priv;
	atomic_bool is_running;
	struct os_mutex *init_mutex;
};

void modvm_accel_backend_register(const struct modvm_accel_backend *backend);
const struct modvm_accel_backend *modvm_accel_backend_find(const char *name);

int modvm_accel_init(struct modvm_accel *accel, const char *name);
int modvm_accel_setup_irqchip(struct modvm_accel *accel);
int modvm_accel_set_irq(struct modvm_accel *accel, uint32_t gsi, int level);
int modvm_accel_map_ram(struct modvm_accel *accel, uint64_t gpa, size_t size,
			uint32_t flags);
void modvm_accel_destroy(struct modvm_accel *accel);

#endif /* MODVM_CORE_ACCEL_H */