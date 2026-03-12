/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_ACCEL_KVM_INTERNAL_H
#define MODVM_ACCEL_KVM_INTERNAL_H

#include <linux/kvm.h>
#include <modvm/core/vcpu.h>

/**
 * struct modvm_kvm_state - KVM specific virtual machine acceleration state
 * @kvm_fd: global handle to the hypervisor character device
 * @vm_fd: handle to this specific virtual machine instance
 * @mem_slot_idx: counter for allocating sequential hardware memory slots
 */
struct modvm_kvm_state {
	int kvm_fd;
	int vm_fd;
	int mem_slot_idx;
};

/**
 * struct modvm_kvm_vcpu_state - KVM specific virtual processor state
 * @vcpu_fd: handle to this specific virtual processor
 * @run_size: size of the memory-mapped hypervisor run structure
 * @run: shared memory region for hypervisor communication
 */
struct modvm_kvm_vcpu_state {
	int vcpu_fd;
	int run_size;
	struct kvm_run *run;
};

int modvm_kvm_arch_vcpu_init(struct modvm_vcpu *vcpu);
int modvm_kvm_arch_vcpu_get_regs(struct modvm_vcpu *vcpu,
				 enum modvm_reg_class reg_class, void *buf,
				 size_t size);
int modvm_kvm_arch_vcpu_set_regs(struct modvm_vcpu *vcpu,
				 enum modvm_reg_class reg_class,
				 const void *buf, size_t size);
int modvm_kvm_arch_vcpu_handle_exit(struct modvm_vcpu *vcpu,
				    struct kvm_run *run);

extern const struct modvm_vcpu_ops modvm_kvm_vcpu_ops;

#endif /* MODVM_ACCEL_KVM_INTERNAL_H */