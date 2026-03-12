/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_VCPU_H
#define MODVM_CORE_VCPU_H

#include <stdint.h>
#include <modvm/core/accel.h>

struct modvm_vcpu;

/**
 * struct modvm_vcpu_ops - virtual processor hardware operations
 * @init: allocate and map hardware vCPU resources
 * @destroy: release hardware vCPU resources
 * @set_pc: configure the physical address of the instruction pointer
 * @run: enter the hardware virtualization execution loop
 */
struct modvm_vcpu_ops {
	int (*init)(struct modvm_vcpu *vcpu);
	void (*destroy)(struct modvm_vcpu *vcpu);
	int (*set_pc)(struct modvm_vcpu *vcpu, uint64_t pc);
	int (*run)(struct modvm_vcpu *vcpu);
};

/**
 * struct modvm_vcpu - represents a single virtual processor core
 * @id: sequential index or architectural ID (e.g., APIC ID / MPIDR)
 * @accel: pointer to the parent virtualization engine context
 * @ops: cached pointer to the hardware operations table
 * @priv: opaque pointer to accelerator-specific CPU state
 */
struct modvm_vcpu {
	int id;
	struct modvm_accel *accel;
	const struct modvm_vcpu_ops *ops;
	void *priv;
};

int modvm_vcpu_init(struct modvm_vcpu *vcpu, struct modvm_accel *accel, int id);
int modvm_vcpu_set_pc(struct modvm_vcpu *vcpu, uint64_t pc);
int modvm_vcpu_run(struct modvm_vcpu *vcpu);
void modvm_vcpu_destroy(struct modvm_vcpu *vcpu);

#endif /* MODVM_CORE_VCPU_H */