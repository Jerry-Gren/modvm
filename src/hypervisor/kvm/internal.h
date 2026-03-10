/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_KVM_INTERNAL_H
#define MODVM_KVM_INTERNAL_H

#include <linux/kvm.h>
#include <modvm/core/vcpu.h>

/**
 * struct kvm_state - KVM specific virtual machine acceleration state.
 * @kvm_fd: global handle to the hypervisor character device (/dev/kvm).
 * @vm_fd: handle to this specific virtual machine instance.
 * @mem_slot_idx: counter for allocating sequential hardware memory slots.
 */
struct kvm_state {
	int kvm_fd;
	int vm_fd;
	int mem_slot_idx;
};

/**
 * struct kvm_vcpu_state - KVM specific virtual processor state.
 * @vcpu_fd: handle to this specific virtual processor.
 * @run_size: size of the memory-mapped hypervisor run structure.
 * @run: shared memory region for hypervisor communication.
 */
struct kvm_vcpu_state {
	int vcpu_fd;
	int run_size;
	struct kvm_run *run;
};

int kvm_arch_vcpu_set_pc(struct vm_vcpu *vcpu, uint64_t pc);

int kvm_arch_vcpu_handle_exit(struct vm_vcpu *vcpu, struct kvm_run *run);

extern const struct vm_vcpu_ops kvm_vcpu_ops;

#endif /* MODVM_KVM_INTERNAL_H */