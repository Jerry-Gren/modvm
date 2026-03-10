/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_VIRTUAL_CPU_H
#define MODVM_CORE_VIRTUAL_CPU_H

#include <stdint.h>
#include <modvm/core/hypervisor.h>

/**
 * struct vm_virtual_cpu - represents a single virtual processor core
 * @cpu_id: the architectural APIC ID or sequential index of this processor
 * @hypervisor: pointer to the parent virtualization engine context
 * @hypervisor_private_data: opaque pointer to accelerator-specific CPU state
 */
struct vm_virtual_cpu {
	int cpu_id;
	struct vm_hypervisor *hypervisor;
	void *hypervisor_private_data;
};

int vm_virtual_cpu_create(struct vm_virtual_cpu *cpu,
			  struct vm_hypervisor *hypervisor, int cpu_id);

int vm_virtual_cpu_set_instruction_pointer(struct vm_virtual_cpu *cpu,
					   uint64_t entry_point);

int vm_virtual_cpu_run(struct vm_virtual_cpu *cpu);

void vm_virtual_cpu_destroy(struct vm_virtual_cpu *cpu);

#endif /* MODVM_CORE_VIRTUAL_CPU_H */