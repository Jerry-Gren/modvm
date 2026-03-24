/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_VCPU_H
#define MODVM_CORE_VCPU_H

#include <stdint.h>
#include <modvm/utils/stddef.h>

struct modvm_accel;
struct modvm_vcpu;

/**
 * enum modvm_reg_class - generic identifiers for architectural register groups
 * @MODVM_REG_GPR: general purpose registers (e.g., RAX on x86, X0 on ARM)
 * @MODVM_REG_SREGS: special and system control registers (e.g., CR0 on x86, SCTLR_EL1 on ARM)
 */
enum modvm_reg_class {
	MODVM_REG_GPR,
	MODVM_REG_SREGS,
};

/**
 * struct modvm_vcpu_ops - backend operations for virtual processors
 * @init: initialize processor state
 * @destroy: release processor resources
 * @get_regs: read full architectural register group (for migration/debug)
 * @set_regs: write full architectural register group (for migration/debug)
 * @get_reg: read a single architectural register by abstract ID
 * @set_reg: write a single architectural register by abstract ID
 * @run: enter hardware/emulator execution loop
 */
struct modvm_vcpu_ops {
	int (*init)(struct modvm_vcpu *vcpu);
	void (*destroy)(struct modvm_vcpu *vcpu);
	int (*get_regs)(struct modvm_vcpu *vcpu, enum modvm_reg_class reg_class,
			void *buf, size_t size);
	int (*set_regs)(struct modvm_vcpu *vcpu, enum modvm_reg_class reg_class,
			const void *buf, size_t size);
	int (*get_reg)(struct modvm_vcpu *vcpu, uint64_t reg_id, uint64_t *val);
	int (*set_reg)(struct modvm_vcpu *vcpu, uint64_t reg_id, uint64_t val);
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
int modvm_vcpu_get_regs(struct modvm_vcpu *vcpu, enum modvm_reg_class reg_class,
			void *buf, size_t size);
int modvm_vcpu_set_regs(struct modvm_vcpu *vcpu, enum modvm_reg_class reg_class,
			const void *buf, size_t size);
int modvm_vcpu_get_reg(struct modvm_vcpu *vcpu, uint64_t reg_id, uint64_t *val);
int modvm_vcpu_set_reg(struct modvm_vcpu *vcpu, uint64_t reg_id, uint64_t val);
int modvm_vcpu_run(struct modvm_vcpu *vcpu);
void modvm_vcpu_destroy(struct modvm_vcpu *vcpu);

#endif /* MODVM_CORE_VCPU_H */