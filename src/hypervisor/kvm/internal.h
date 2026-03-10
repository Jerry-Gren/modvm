/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_KVM_INTERNAL_H
#define MODVM_KVM_INTERNAL_H

#include <linux/kvm.h>

#include <modvm/core/virtual_cpu.h>

/**
 * struct vm_kvm_state - KVM specific virtual machine acceleration state
 * @kvm_file_descriptor: global handle to the hypervisor character device
 * @virtual_machine_file_descriptor: handle to this specific virtual machine
 * @memory_slot_index: counter for allocating sequential hardware memory slots
 */
struct vm_kvm_state {
	int kvm_file_descriptor;
	int virtual_machine_file_descriptor;
	int memory_slot_index;
};

/**
 * struct vm_kvm_virtual_cpu_state - KVM specific virtual processor state
 * @virtual_cpu_file_descriptor: handle to this specific virtual processor
 * @kvm_run_mapping_size_bytes: size of the memory-mapped hypervisor run structure
 * @kvm_run_structure: shared memory region for hypervisor communication
 */
struct vm_kvm_virtual_cpu_state {
	int virtual_cpu_file_descriptor;
	int kvm_run_mapping_size_bytes;
	struct kvm_run *kvm_run_structure;
};

/*
 * Architecture-specific KVM backend hooks.
 * These must be implemented by each supported target architecture.
 */
int kvm_arch_vcpu_set_pc(struct vm_virtual_cpu *cpu, uint64_t pc);
int kvm_arch_vcpu_handle_exit(struct vm_virtual_cpu *cpu, struct kvm_run *run);

#endif /* MODVM_KVM_INTERNAL_H */