/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_MACHINE_H
#define MODVM_MACHINE_H

#include <stddef.h>
#include <stdint.h>

#include <modvm/vm.h>
#include <modvm/vcpu.h>
#include <modvm/os_thread.h>
#include <modvm/chardev.h>

struct machine;

/**
 * struct vm_machine_class - describes a specific hardware topology
 * @name: short identifier used in command line arguments
 * @desc: human-readable description of the emulated board
 * @init: callback to assemble architecture-specific peripherals and memory
 * @reset: Callback to configure initial CPU states and load firmware
 *
 * This abstraction allows the core engine to support diverse platforms
 * (e.g., x86 PC, ARM64 Virt) without compiling hardware specifics into
 * the common control plane.
 */
struct vm_machine_class {
	const char *name;
	const char *desc;
	int (*init)(struct machine *mach);
	int (*reset)(struct machine *mach);
};

/**
 * struct machine_config - hardware configuration supplied by the user
 * @ram_base: guest physical address where the primary memory starts
 * @ram_size: total guest physical memory in bytes
 * @smp_cpus: number of virtual CPUs to instantiate
 * @kernel_path: host filesystem path to the guest firmware or kernel
 * @machine_type: pointer to the selected machine class descriptor
 * @serial_backend: host character device mapped to the primary console
 */
struct machine_config {
	uint64_t ram_base;
	size_t ram_size;
	unsigned int smp_cpus;
	const char *kernel_path;
	const struct vm_machine_class *machine_type;
	struct vm_chardev *serial_backend;
};

/**
 * struct machine - the top-level motherboard abstraction
 * @config: the immutable configuration for this boot session
 * @vm: the hypervisor virtual machine container
 * @vcpus: array of pointers to virtual CPU instances
 * @threads: array of abstract OS thread handles driving the vCPUs
 */
struct machine {
	struct machine_config config;
	struct vm vm;
	struct vcpu **vcpus;
	struct os_thread **threads;
};

void vm_machine_class_register(const struct vm_machine_class *cls);
const struct vm_machine_class *vm_machine_class_find(const char *name);

/**
 * machine_init - assemble the virtual motherboard and peripherals
 * @mach: the machine instance to initialize
 * @cfg: the desired hardware topology
 *
 * Return: 0 on success, negative error code on failure.
 */
int machine_init(struct machine *mach, const struct machine_config *cfg);

/**
 * machine_run - power on the machine and spawn vCPU threads
 * @mach: the fully assembled machine instance
 *
 * This function blocks until all vCPU threads terminate or a fatal
 * hardware event forces a shutdown.
 *
 * Return: 0 on clean shutdown, negative error code on fatal errors.
 */
int machine_run(struct machine *mach);

/**
 * machine_request_shutdown - initiate an ACPI-like graceful shutdown
 * @mach: the machine to power off
 *
 * Safely alters the global power state and sends hardware wake-up signals
 * to all sleeping virtual processors, causing them to exit their run loops.
 */
void machine_request_shutdown(struct machine *mach);

/**
 * machine_destroy - power off the machine and release all memory
 * @mach: the machine instance to destroy
 */
void machine_destroy(struct machine *mach);

#endif /* MODVM_MACHINE_H */