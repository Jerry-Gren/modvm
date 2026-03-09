/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_VCPU_H
#define MODVM_VCPU_H

#include <stdint.h>

#include <modvm/vm.h>

struct arch_vcpu;

/**
 * struct vcpu - represents a single virtual processor
 * @id: the architectural APIC ID or sequential index of this vCPU
 * @vm: pointer to the parent virtual machine container
 * @arch: opaque pointer to hypervisor-specific vCPU state
 */
struct vcpu {
	int id;
	struct vm *vm;
	struct arch_vcpu *arch;
};

/**
 * vcpu_create - instantiate a new virtual CPU within a VM
 * @vcpu: the uninitialized vcpu object
 * @vm: the parent virtual machine
 * @id: the desired vCPU identifier
 *
 * Return: 0 on success, negative error code on failure.
 */
int vcpu_create(struct vcpu *vcpu, struct vm *vm, int id);

/**
 * vcpu_set_pc - set the Program Counter (Instruction Pointer) of the vCPU
 * @vcpu: the initialized vcpu object
 * @entry_point: the guest physical address (GPA) where execution should begin
 *
 * This forces the virtual CPU to start fetching instructions from the
 * specified physical address, bypassing the default architectural reset vector.
 *
 * Return: 0 on success, negative error code on failure.
 */
int vcpu_set_pc(struct vcpu *vcpu, uint64_t entry_point);

/**
 * vcpu_run - enter the guest execution loop
 * @vcpu: the initialized vcpu object
 * @running_flag: atomic boolean flag indicating if the system has powered off
 *
 * Return: 0 on clean shutdown, negative error code on fatal hypervisor error.
 */
int vcpu_run(struct vcpu *vcpu);

/**
 * vcpu_destroy - release resources associated with a virtual CPU
 * @vcpu: the vcpu object to destroy
 */
void vcpu_destroy(struct vcpu *vcpu);

#endif /* MODVM_VCPU_H */