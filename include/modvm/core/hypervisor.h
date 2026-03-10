/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_HYPERVISOR_H
#define MODVM_CORE_HYPERVISOR_H

#include <stdatomic.h>
#include <modvm/core/memory.h>
#include <modvm/os/thread.h>

/**
 * struct vm_hypervisor - represents the core virtualization engine context
 * @memory_space: the physical memory controller managing guest mappings
 * @hypervisor_private_data: opaque pointer to the underlying accelerator state
 * @is_running: thread-safe power state monitored by all executing processors
 *
 * This structure establishes the execution boundary. It securely isolates
 * the architecture-agnostic core logic from the specific hypervisor backend
 * implementation details, preventing host platform header leakage.
 */
struct vm_hypervisor {
	struct vm_memory_space memory_space;
	void *hypervisor_private_data;
	atomic_bool is_running;
	struct os_mutex *startup_synchronization_lock;
};

int vm_hypervisor_create(struct vm_hypervisor *hypervisor);

int vm_hypervisor_setup_interrupt_controller(struct vm_hypervisor *hypervisor);

int vm_hypervisor_set_interrupt_line(struct vm_hypervisor *hypervisor,
				     uint32_t line_number, int level);

void vm_hypervisor_destroy(struct vm_hypervisor *hypervisor);

#endif /* MODVM_CORE_HYPERVISOR_H */