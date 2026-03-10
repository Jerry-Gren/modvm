/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_MACHINE_H
#define MODVM_CORE_MACHINE_H

#include <stdint.h>

#include <modvm/core/hypervisor.h>
#include <modvm/core/vcpu.h>
#include <modvm/os/thread.h>
#include <modvm/core/chardev.h>

struct vm_machine;

/**
 * struct vm_machine_ops - runtime operations for an instantiated machine.
 * @init: wire up hardware components and allocate resources.
 * @reset: configure initial CPU states and load firmware.
 *
 * Encapsulates the runtime behavior of a specific hardware topology.
 */
struct vm_machine_ops {
	int (*init)(struct vm_machine *machine);
	int (*reset)(struct vm_machine *machine);
};

/**
 * struct vm_machine_class - static blueprint for machine topologies.
 * @name: identifier used in command line arguments.
 * @desc: human-readable description of the emulated board.
 * @ops: pointer to the operational methods for this machine type.
 */
struct vm_machine_class {
	const char *name;
	const char *desc;
	const struct vm_machine_ops *ops;
};

/**
 * struct vm_machine_config - user supplied hardware parameters.
 * @ram_base: guest physical address where primary memory starts.
 * @ram_size: total guest physical memory in bytes.
 * @nr_vcpus: number of virtual processors to instantiate.
 * @firmware_path: host filesystem path to the guest firmware.
 * @machine_class: pointer to the selected machine blueprint.
 * @console: host character device mapped to the primary console.
 */
struct vm_machine_config {
	uint64_t ram_base;
	size_t ram_size;
	unsigned int nr_vcpus;
	const char *firmware_path;
	const struct vm_machine_class *machine_class;
	struct vm_chardev *console;
};

/**
 * struct vm_machine - the motherboard abstraction.
 * @config: immutable configuration for this session.
 * @hv: virtualization engine container.
 * @vcpus: array of virtual processor instances.
 * @vcpu_threads: array of host OS threads driving the processors.
 */
struct vm_machine {
	struct vm_machine_config config;
	struct vm_hypervisor hv;
	struct vm_vcpu **vcpus;
	struct os_thread **vcpu_threads;
};

void vm_machine_class_register(const struct vm_machine_class *cls);

const struct vm_machine_class *vm_machine_class_find(const char *name);

int vm_machine_init(struct vm_machine *machine,
		    const struct vm_machine_config *config);

int vm_machine_run(struct vm_machine *machine);

void vm_machine_request_shutdown(struct vm_machine *machine);

void vm_machine_destroy(struct vm_machine *machine);

#endif /* MODVM_CORE_MACHINE_H */