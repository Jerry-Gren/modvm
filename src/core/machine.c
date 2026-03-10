/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/core/machine.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>
#include <modvm/os/thread.h>
#include <modvm/os/event_loop.h>

#undef pr_fmt
#define pr_fmt(fmt) "machine: " fmt

static void *vcpu_thread_fn(void *data)
{
	struct vm_vcpu *vcpu = data;
	int ret;

	ret = vm_vcpu_run(vcpu);
	if (ret < 0)
		pr_err("fatal execution error on vcpu %d\n", vcpu->id);

	return NULL;
}

/**
 * vm_machine_init - assemble the virtual machine topology.
 * @machine: the machine object to initialize.
 * @config: the immutable configuration parameters.
 *
 * Allocates processors, initializes the core hypervisor context,
 * maps primary RAM, and invokes the board-specific initialization hook.
 *
 * return: 0 on success, or a negative error code on failure.
 */
int vm_machine_init(struct vm_machine *machine,
		    const struct vm_machine_config *config)
{
	unsigned int i;
	int ret;

	if (WARN_ON(!machine || !config))
		return -EINVAL;

	machine->config = *config;

	/* Bring up the hypervisor container to establish the execution boundary */
	ret = vm_hypervisor_init(&machine->hv, machine->config.accel_name);
	if (ret < 0) {
		pr_err("failed to instantiate hypervisor context\n");
		return ret;
	}

	ret = vm_mem_region_add(&machine->hv.mem_space,
				machine->config.ram_base,
				machine->config.ram_size,
				VM_MEM_FLAG_READONLY | VM_MEM_FLAG_EXEC);
	if (ret < 0) {
		pr_err("failed to map primary system memory\n");
		goto err_destroy_hv;
	}

	if (machine->config.machine_class &&
	    machine->config.machine_class->ops &&
	    machine->config.machine_class->ops->init) {
		ret = machine->config.machine_class->ops->init(machine);
		if (ret < 0) {
			pr_err("machine class failed to initialize peripherals\n");
			goto err_destroy_hv;
		}
	}

	machine->vcpus =
		calloc(machine->config.nr_vcpus, sizeof(struct vm_vcpu *));
	machine->vcpu_threads =
		calloc(machine->config.nr_vcpus, sizeof(struct os_thread *));
	if (!machine->vcpus || !machine->vcpu_threads) {
		ret = -ENOMEM;
		goto err_free_topology;
	}

	for (i = 0; i < machine->config.nr_vcpus; i++) {
		machine->vcpus[i] = calloc(1, sizeof(struct vm_vcpu));
		if (!machine->vcpus[i]) {
			ret = -ENOMEM;
			goto err_free_vcpus;
		}

		ret = vm_vcpu_init(machine->vcpus[i], &machine->hv, i);
		if (ret < 0) {
			pr_err("failed to instantiate vcpu %u\n", i);
			free(machine->vcpus[i]);
			machine->vcpus[i] = NULL;
			goto err_free_vcpus;
		}
	}

	pr_info("machine assembled with %zu bytes ram and %u vcpus\n",
		machine->config.ram_size, machine->config.nr_vcpus);

	return 0;

err_free_vcpus:
	while (i > 0) {
		--i;
		if (machine->vcpus[i]) {
			vm_vcpu_destroy(machine->vcpus[i]);
			free(machine->vcpus[i]);
		}
	}
err_free_topology:
	free(machine->vcpus);
	free(machine->vcpu_threads);
err_destroy_hv:
	vm_hypervisor_destroy(&machine->hv);
	return ret;
}

/**
 * vm_machine_run - ignite the processor cores and start the main loop.
 * @machine: the fully initialized machine object.
 *
 * return: 0 upon graceful shutdown, or a negative error code.
 */
int vm_machine_run(struct vm_machine *machine)
{
	unsigned int i;
	int ret;

	if (WARN_ON(!machine))
		return -EINVAL;

	pr_info("powering on the virtual machine...\n");

	ret = vm_event_loop_init();
	if (ret < 0)
		return ret;

	os_thread_system_init();

	atomic_store(&machine->hv.is_running, true);

	if (machine->config.machine_class &&
	    machine->config.machine_class->ops &&
	    machine->config.machine_class->ops->reset) {
		ret = machine->config.machine_class->ops->reset(machine);
		if (ret < 0) {
			pr_err("machine class reset protocol failed\n");
			return ret;
		}
	}

	os_mutex_lock(machine->hv.init_mutex);

	for (i = 0; i < machine->config.nr_vcpus; i++) {
		machine->vcpu_threads[i] =
			os_thread_create(vcpu_thread_fn, machine->vcpus[i]);
		if (IS_ERR(machine->vcpu_threads[i])) {
			pr_err("failed to spawn thread for vcpu %u\n", i);
			os_mutex_unlock(machine->hv.init_mutex);
			return PTR_ERR(machine->vcpu_threads[i]);
		}
	}

	os_mutex_unlock(machine->hv.init_mutex);

	/*
	 * The primary host thread transforms into the system event dispatcher.
	 * It will block indefinitely, routing peripheral I/O, until a shutdown
	 * request is intercepted.
	 */
	vm_event_loop_run();

	for (i = 0; i < machine->config.nr_vcpus; i++) {
		if (machine->vcpu_threads[i])
			os_thread_join(machine->vcpu_threads[i]);
	}

	pr_info("machine powered off gracefully\n");
	return 0;
}

/**
 * vm_machine_request_shutdown - Broadcast a termination signal to all cores.
 * @machine: The running machine object.
 */
void vm_machine_request_shutdown(struct vm_machine *machine)
{
	unsigned int i;

	if (!machine)
		return;

	atomic_store(&machine->hv.is_running, false);

	for (i = 0; i < machine->config.nr_vcpus; i++) {
		if (machine->vcpu_threads[i])
			os_thread_send_wakeup(machine->vcpu_threads[i]);
	}

	vm_event_loop_stop();
}

/**
 * vm_machine_destroy - teardown topology and release host resources.
 * @machine: the machine object to destroy.
 */
void vm_machine_destroy(struct vm_machine *machine)
{
	unsigned int i;

	if (!machine)
		return;

	if (machine->vcpus) {
		for (i = 0; i < machine->config.nr_vcpus; i++) {
			if (machine->vcpus[i]) {
				vm_vcpu_destroy(machine->vcpus[i]);
				free(machine->vcpus[i]);
			}
		}
		free(machine->vcpus);
		machine->vcpus = NULL;
	}

	if (machine->vcpu_threads) {
		for (i = 0; i < machine->config.nr_vcpus; i++) {
			if (machine->vcpu_threads[i])
				os_thread_destroy(machine->vcpu_threads[i]);
		}
		free(machine->vcpu_threads);
		machine->vcpu_threads = NULL;
	}

	vm_hypervisor_destroy(&machine->hv);
}