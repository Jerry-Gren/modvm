/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_HYPERVISOR_H
#define MODVM_CORE_HYPERVISOR_H

#include <stdatomic.h>
#include <modvm/core/memory.h>
#include <modvm/os/thread.h>

/**
 * struct vm_hypervisor - the virtualization engine context.
 * @mem_space: physical memory controller managing guest mappings.
 * @priv: opaque pointer to the underlying accelerator state.
 * @is_running: thread-safe power state monitored by all executing processors.
 * @init_mutex: synchronization lock for vCPU startup phase.
 *
 * Secures the architecture-agnostic core logic from specific backend
 * details, preventing host platform header leakage.
 */
struct vm_hypervisor {
	struct vm_mem_space mem_space;
	void *priv;
	atomic_bool is_running;
	struct os_mutex *init_mutex;
};

int vm_hypervisor_create(struct vm_hypervisor *hv);

int vm_hypervisor_setup_irqchip(struct vm_hypervisor *hv);

int vm_hypervisor_set_irq(struct vm_hypervisor *hv, uint32_t irq, int level);

void vm_hypervisor_destroy(struct vm_hypervisor *hv);

#endif /* MODVM_CORE_HYPERVISOR_H */