/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_VCPU_H
#define MODVM_CORE_VCPU_H

#include <stdint.h>
#include <modvm/core/hypervisor.h>

struct vm_vcpu;

/**
 * struct vm_vcpu_ops - virtual processor hardware operations.
 * @init: allocate and map hardware vCPU resources.
 * @destroy: release hardware vCPU resources.
 * @set_pc: configure the physical address of the instruction pointer.
 * @run: enter the hardware virtualization execution loop.
 */
struct vm_vcpu_ops {
	int (*init)(struct vm_vcpu *vcpu);
	void (*destroy)(struct vm_vcpu *vcpu);
	int (*set_pc)(struct vm_vcpu *vcpu, uint64_t pc);
	int (*run)(struct vm_vcpu *vcpu);
};

/**
 * struct vm_vcpu - represents a single virtual processor core.
 * @id: sequential index or architectural ID (e.g., APIC ID / MPIDR).
 * @hv: pointer to the parent virtualization engine context.
 * @ops: cached pointer to the hardware operations table.
 * @priv: opaque pointer to accelerator-specific CPU state.
 */
struct vm_vcpu {
	int id;
	struct vm_hypervisor *hv;
	const struct vm_vcpu_ops *ops;
	void *priv;
};

int vm_vcpu_init(struct vm_vcpu *vcpu, struct vm_hypervisor *hv, int id);

int vm_vcpu_set_pc(struct vm_vcpu *vcpu, uint64_t pc);

int vm_vcpu_run(struct vm_vcpu *vcpu);

void vm_vcpu_destroy(struct vm_vcpu *vcpu);

#endif /* MODVM_CORE_VCPU_H */