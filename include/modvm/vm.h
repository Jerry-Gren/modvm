/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_VM_H
#define MODVM_VM_H

#include <stdatomic.h>

#include <modvm/memory.h>

struct arch_vm;

/**
 * struct vm - represents a single virtual machine instance
 * @mem_space: the physical memory controller for this VM
 * @arch: opaque pointer to architecture/hypervisor specific internal state
 * @running: Thread-safe power state, monitored by all executing vCPUs
 *
 * This is the top-level container for a virtual machine. It ties together
 * the memory slots, virtual CPUs, and the underlying hypervisor context.
 */
struct vm {
	struct vm_memory_space mem_space;
	struct arch_vm *arch;
	atomic_bool running;
};

/**
 * vm_create - initialize a new virtual machine instance
 * @vm: the uninitialized vm object
 *
 * This function opens the hypervisor interface, creates the VM context,
 * and initializes the memory space with the appropriate mapping callbacks.
 *
 * Return: 0 on success, negative error code on failure.
 */
int vm_create(struct vm *vm);

/**
 * vm_arch_setup_irqchip - initialize the architectural interrupt controllers
 * @vm: the virtual machine instance
 *
 * This instantiates the hardware necessary for timer ticks and external
 * device interrupts. It must be explicitly invoked by the specific machine
 * class initialization routine (e.g., machine_pc_init), not by the core VMM.
 *
 * Return: 0 on success, negative error code on failure.
 */
int vm_arch_setup_irqchip(struct vm *vm);

/**
 * vm_set_irq - assert or deassert a hardware interrupt line
 * @vm: the core virtual machine context
 * @irq: the interrupt line number (e.g., Global System Interrupt)
 * @level: 1 to assert the interrupt, 0 to deassert
 *
 * This provides a hardware-agnostic interface for virtual peripherals
 * to signal the hypervisor.
 */
int vm_set_irq(struct vm *vm, uint32_t irq, int level);

/**
 * vm_destroy - tear down a virtual machine and release all resources
 * @vm: the vm object to destroy
 */
void vm_destroy(struct vm *vm);

#endif /* MODVM_VM_H */