/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_VCPU_H
#define MODVM_CORE_VCPU_H

#include <stdint.h>
#include <modvm/core/hypervisor.h>

/**
 * struct vm_vcpu - represents a single virtual processor core.
 * @id: sequential index or architectural ID (e.g., APIC ID / MPIDR) of this processor.
 * @hv: pointer to the parent virtualization engine context.
 * @priv: opaque pointer to accelerator-specific CPU state.
 */
struct vm_vcpu {
	int id;
	struct vm_hypervisor *hv;
	void *priv;
};

int vm_vcpu_create(struct vm_vcpu *vcpu, struct vm_hypervisor *hv, int id);

int vm_vcpu_set_pc(struct vm_vcpu *vcpu, uint64_t pc);

int vm_vcpu_run(struct vm_vcpu *vcpu);

void vm_vcpu_destroy(struct vm_vcpu *vcpu);

#endif /* MODVM_CORE_VCPU_H */