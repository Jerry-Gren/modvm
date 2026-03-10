/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_HYPERVISOR_H
#define MODVM_CORE_HYPERVISOR_H

#include <stdatomic.h>
#include <modvm/core/memory.h>
#include <modvm/os/thread.h>

struct vm_hypervisor;
struct vm_vcpu_ops;

/**
 * struct vm_hv_ops - hypervisor backend operations.
 * @init: initialize the hardware acceleration context.
 * @destroy: release hardware acceleration resources.
 * @setup_irqchip: synthesize the architectural interrupt controller.
 * @set_irq: assert or deassert a specific hardware interrupt line.
 */
struct vm_hv_ops {
	int (*init)(struct vm_hypervisor *hv);
	void (*destroy)(struct vm_hypervisor *hv);
	int (*setup_irqchip)(struct vm_hypervisor *hv);
	int (*set_irq)(struct vm_hypervisor *hv, uint32_t gsi, int level);
};

/**
 * struct vm_hv_class - blueprint for a hypervisor backend.
 * @name: the unique string identifier (e.g., "kvm", "whpx").
 * @ops: hypervisor operations.
 * @vcpu_ops: virtual processor operations associated with this backend.
 */
struct vm_hv_class {
	const char *name;
	const struct vm_hv_ops *ops;
	const struct vm_vcpu_ops *vcpu_ops;
};

/**
 * struct vm_hypervisor - the virtualization engine context.
 * @cls: the blueprint of the selected acceleration backend.
 * @mem_space: physical memory controller managing guest mappings.
 * @priv: opaque pointer to the underlying accelerator state.
 * @is_running: thread-safe power state monitored by all executing processors.
 * @init_mutex: synchronization lock for vCPU startup phase.
 */
struct vm_hypervisor {
	const struct vm_hv_class *cls;
	struct vm_mem_space mem_space;
	void *priv;
	atomic_bool is_running;
	struct os_mutex *init_mutex;
};

void vm_hv_class_register(const struct vm_hv_class *cls);

const struct vm_hv_class *vm_hv_class_find(const char *name);

int vm_hypervisor_init(struct vm_hypervisor *hv, const char *accel_name);

int vm_hypervisor_setup_irqchip(struct vm_hypervisor *hv);

int vm_hypervisor_set_irq(struct vm_hypervisor *hv, uint32_t gsi, int level);

void vm_hypervisor_destroy(struct vm_hypervisor *hv);

#endif /* MODVM_CORE_HYPERVISOR_H */