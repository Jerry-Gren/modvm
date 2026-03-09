/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_KVM_INTERNAL_H
#define MODVM_KVM_INTERNAL_H

#include <linux/kvm.h>

/**
 * struct arch_vm - KVM specific virtual machine state
 * @kvm_fd: the global handle to the /dev/kvm character device
 * @vm_fd: the handle to this specific virtual machine instance
 * @mem_slot_index: counter for allocating sequential KVM memory slots
 */
struct arch_vm {
	int kvm_fd;
	int vm_fd;
	int mem_slot_index;
};

/**
 * struct arch_vcpu - KVM specific virtual CPU state
 * @vcpu_fd: the handle to this specific vCPU
 * @run_sz: the size of the mmap'ed KVM run structure
 * @kvm_run: shared memory region for hypervisor-to-userspace communication
 */
struct arch_vcpu {
	int vcpu_fd;
	int run_sz;
	struct kvm_run *kvm_run;
};

#endif /* MODVM_KVM_INTERNAL_H */