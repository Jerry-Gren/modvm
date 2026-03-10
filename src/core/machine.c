/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/machine.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>
#include <modvm/os/thread.h>
#include <modvm/core/event_loop.h>

#undef pr_fmt
#define pr_fmt(fmt) "machine: " fmt

static void *virtual_cpu_thread_entry(void *context_data)
{
	struct vm_virtual_cpu *cpu = context_data;
	int return_code;

	return_code = vm_virtual_cpu_run(cpu);
	if (return_code < 0)
		pr_err("fatal execution error on virtual processor %d\n",
		       cpu->cpu_id);

	return NULL;
}

/**
 * vm_machine_init - assemble the virtual machine topology
 * @machine: the machine object to initialize
 * @config: the immutable configuration parameters
 *
 * Allocates processors, initializes the core hypervisor context,
 * maps primary RAM, and invokes the board-specific initialization hook.
 *
 * return: 0 on success, or a negative error code on failure.
 */
int vm_machine_init(struct vm_machine *machine,
		    const struct vm_machine_config *config)
{
	unsigned int index;
	int return_code;

	if (WARN_ON(!machine || !config))
		return -EINVAL;

	machine->config = *config;

	/* Bring up the hypervisor container to establish the execution boundary */
	return_code = vm_hypervisor_create(&machine->hypervisor);
	if (return_code < 0) {
		pr_err("failed to instantiate hypervisor context\n");
		return return_code;
	}

	return_code = vm_memory_region_add(&machine->hypervisor.memory_space,
					   machine->config.memory_base_address,
					   machine->config.memory_size_bytes,
					   VM_MEMORY_FLAG_READONLY |
						   VM_MEMORY_FLAG_EXECUTABLE);
	if (return_code < 0) {
		pr_err("failed to map primary system memory\n");
		goto error_destroy_hypervisor;
	}

	if (machine->config.machine_class &&
	    machine->config.machine_class->operations &&
	    machine->config.machine_class->operations->init) {
		return_code = machine->config.machine_class->operations->init(
			machine);
		if (return_code < 0) {
			pr_err("machine class failed to initialize peripherals\n");
			goto error_destroy_hypervisor;
		}
	}

	machine->virtual_cpus = calloc(machine->config.processor_count,
				       sizeof(struct vm_virtual_cpu *));
	machine->virtual_cpu_threads = calloc(machine->config.processor_count,
					      sizeof(struct os_thread *));
	if (!machine->virtual_cpus || !machine->virtual_cpu_threads) {
		return_code = -ENOMEM;
		goto error_free_topology;
	}

	for (index = 0; index < machine->config.processor_count; index++) {
		machine->virtual_cpus[index] =
			calloc(1, sizeof(struct vm_virtual_cpu));
		if (!machine->virtual_cpus[index]) {
			return_code = -ENOMEM;
			goto error_free_virtual_cpus;
		}

		return_code =
			vm_virtual_cpu_create(machine->virtual_cpus[index],
					      &machine->hypervisor, index);
		if (return_code < 0) {
			pr_err("failed to instantiate virtual processor %u\n",
			       index);
			free(machine->virtual_cpus[index]);
			machine->virtual_cpus[index] = NULL;
			goto error_free_virtual_cpus;
		}
	}

	pr_info("machine assembled with %zu bytes ram and %u processors\n",
		machine->config.memory_size_bytes,
		machine->config.processor_count);

	return 0;

error_free_virtual_cpus:
	while (index > 0) {
		--index;
		if (machine->virtual_cpus[index]) {
			vm_virtual_cpu_destroy(machine->virtual_cpus[index]);
			free(machine->virtual_cpus[index]);
		}
	}
error_free_topology:
	free(machine->virtual_cpus);
	free(machine->virtual_cpu_threads);
error_destroy_hypervisor:
	vm_hypervisor_destroy(&machine->hypervisor);
	return return_code;
}

/**
 * vm_machine_run - ignite the processor cores and start the main loop
 * @machine: the fully initialized machine object
 *
 * return: 0 upon graceful shutdown, or a negative error code.
 */
int vm_machine_run(struct vm_machine *machine)
{
	unsigned int index;
	int return_code;

	if (WARN_ON(!machine))
		return -EINVAL;

	pr_info("powering on the virtual machine...\n");

	return_code = vm_event_loop_init();
	if (return_code < 0)
		return return_code;

	os_thread_system_init();

	atomic_store(&machine->hypervisor.is_running, true);

	if (machine->config.machine_class &&
	    machine->config.machine_class->operations &&
	    machine->config.machine_class->operations->reset) {
		return_code = machine->config.machine_class->operations->reset(
			machine);
		if (return_code < 0) {
			pr_err("machine class reset protocol failed\n");
			return return_code;
		}
	}

	os_mutex_lock(machine->hypervisor.startup_synchronization_lock);

	for (index = 0; index < machine->config.processor_count; index++) {
		machine->virtual_cpu_threads[index] = os_thread_create(
			virtual_cpu_thread_entry, machine->virtual_cpus[index]);

		if (IS_ERR(machine->virtual_cpu_threads[index])) {
			pr_err("failed to spawn execution thread for virtual processor %u\n",
			       index);
			os_mutex_unlock(machine->hypervisor
						.startup_synchronization_lock);
			return PTR_ERR(machine->virtual_cpu_threads[index]);
		}
	}

	os_mutex_unlock(machine->hypervisor.startup_synchronization_lock);

	/*
	 * The primary host thread transforms into the system event dispatcher.
	 * It will block indefinitely, routing peripheral I/O, until a shutdown
	 * request is intercepted.
	 */
	vm_event_loop_run();

	for (index = 0; index < machine->config.processor_count; index++) {
		if (machine->virtual_cpu_threads[index]) {
			os_thread_join(machine->virtual_cpu_threads[index]);
		}
	}

	pr_info("machine powered off gracefully\n");
	return 0;
}

/**
 * vm_machine_request_shutdown - broadcast a termination signal to all cores
 * @machine: the running machine object
 */
void vm_machine_request_shutdown(struct vm_machine *machine)
{
	unsigned int index;

	if (!machine)
		return;

	atomic_store(&machine->hypervisor.is_running, false);

	for (index = 0; index < machine->config.processor_count; index++) {
		if (machine->virtual_cpu_threads[index]) {
			os_thread_send_wakeup_signal(
				machine->virtual_cpu_threads[index]);
		}
	}

	vm_event_loop_stop();
}

/**
 * vm_machine_destroy - teardown topology and release host resources
 * @machine: the machine object to destroy
 */
void vm_machine_destroy(struct vm_machine *machine)
{
	unsigned int index;

	if (!machine)
		return;

	if (machine->virtual_cpus) {
		for (index = 0; index < machine->config.processor_count;
		     index++) {
			if (machine->virtual_cpus[index]) {
				vm_virtual_cpu_destroy(
					machine->virtual_cpus[index]);
				free(machine->virtual_cpus[index]);
			}
		}
		free(machine->virtual_cpus);
		machine->virtual_cpus = NULL;
	}

	if (machine->virtual_cpu_threads) {
		for (index = 0; index < machine->config.processor_count;
		     index++) {
			if (machine->virtual_cpu_threads[index]) {
				os_thread_destroy(
					machine->virtual_cpu_threads[index]);
			}
		}
		free(machine->virtual_cpu_threads);
		machine->virtual_cpu_threads = NULL;
	}

	vm_hypervisor_destroy(&machine->hypervisor);
}