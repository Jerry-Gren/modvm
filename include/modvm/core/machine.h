/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_MACHINE_H
#define MODVM_CORE_MACHINE_H

#include <stddef.h>
#include <stdint.h>

#include <modvm/core/hypervisor.h>
#include <modvm/core/virtual_cpu.h>
#include <modvm/os/thread.h>

struct vm_machine;
struct vm_character_device;

/**
 * struct vm_machine_operations - runtime operations for an instantiated machine
 * @init: wire up hardware components and allocate resources
 * @reset: configure initial cpu states and load firmware
 *
 * Encapsulates the runtime behavior of a specific hardware topology.
 * These callbacks operate on a live machine instance.
 */
struct vm_machine_operations {
	int (*init)(struct vm_machine *machine);
	int (*reset)(struct vm_machine *machine);
};

/**
 * struct vm_machine_class - static blueprint for machine topologies
 * @name: identifier used in command line arguments
 * @description: human-readable description of the emulated board
 * @operations: pointer to the operational methods for this machine type
 *
 * This represents the factory definition. It is registered during
 * early boot and used to instantiate specific board types.
 */
struct vm_machine_class {
	const char *name;
	const char *description;
	const struct vm_machine_operations *operations;
};

/**
 * struct vm_machine_config - user supplied hardware parameters
 * @memory_base_address: guest physical address where the primary memory starts
 * @memory_size_bytes: total guest physical memory in bytes
 * @processor_count: number of virtual processors to instantiate
 * @firmware_path: host filesystem path to the guest firmware
 * @machine_class: pointer to the selected machine blueprint
 * @primary_console_backend: host character device mapped to the primary console
 */
struct vm_machine_config {
	uint64_t memory_base_address;
	size_t memory_size_bytes;
	unsigned int processor_count;
	const char *firmware_path;
	const struct vm_machine_class *machine_class;
	struct vm_character_device *primary_console_backend;
};

/**
 * struct vm_machine - the core motherboard abstraction
 * @config: immutable configuration for this session
 * @hypervisor: virtualization engine container
 * @virtual_cpus: array of virtual processor instances
 * @virtual_cpu_threads: array of host operating system threads driving the processors
 */
struct vm_machine {
	struct vm_machine_config config;
	struct vm_hypervisor hypervisor;
	struct vm_virtual_cpu **virtual_cpus;
	struct os_thread **virtual_cpu_threads;
};

void vm_machine_class_register(const struct vm_machine_class *machine_class);

const struct vm_machine_class *vm_machine_class_find(const char *name);

int vm_machine_init(struct vm_machine *machine,
		    const struct vm_machine_config *config);

int vm_machine_run(struct vm_machine *machine);

void vm_machine_request_shutdown(struct vm_machine *machine);

void vm_machine_destroy(struct vm_machine *machine);

#endif /* MODVM_CORE_MACHINE_H */