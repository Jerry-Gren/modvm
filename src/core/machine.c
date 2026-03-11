/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/machine.h>
#include <modvm/core/device.h>
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

void *vm_machm_zalloc(struct vm_machine *machine, size_t size)
{
	void *res = vm_res_alloc(NULL, size);
	if (res) {
		memset(res, 0, size);
		vm_res_add(&machine->machm_pool, res);
	}
	return res;
}

/**
 * vm_machm_strdup - allocate a machine-managed duplicate of a string
 * @machine: the machine object to manage this string's lifecycle
 * @s: the string to duplicate
 *
 * return: pointer to the duplicated string, or NULL on failure.
 */
char *vm_machm_strdup(struct vm_machine *machine, const char *s)
{
	size_t size;
	char *res;

	if (!s)
		return NULL;

	size = strlen(s) + 1;
	res = vm_machm_zalloc(machine, size);
	if (res)
		memcpy(res, s, size);

	return res;
}

int __vm_machm_add_action(struct vm_machine *machine, void (*action)(void *),
			  void *data)
{
	return vm_res_add_action(&machine->machm_pool, action, data);
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

	/* Establish infrastructure anchors and private bus topology */
	INIT_LIST_HEAD(&machine->devices);
	INIT_LIST_HEAD(&machine->bus.pio_regions);
	INIT_LIST_HEAD(&machine->bus.mmio_regions);
	vm_res_pool_init(&machine->machm_pool, machine);

	/*
	 * Initialize the private event loop early so that peripherals can 
	 * safely register their file descriptors during the instantiate phase.
	 */
	ret = vm_event_loop_init(machine);
	if (ret < 0) {
		pr_err("failed to initialize private event loop\n");
		return ret;
	}

	ret = vm_hypervisor_init(&machine->hv, machine->config.accel_name);
	if (ret < 0) {
		pr_err("failed to instantiate hypervisor context\n");
		return ret;
	}

	/* Delegate hypervisor teardown to the machine pool */
	ret = vm_machm_add_action(
		machine, (void (*)(void *))vm_hypervisor_destroy, &machine->hv);
	if (ret < 0) {
		vm_hypervisor_destroy(&machine->hv);
		return ret;
	}

	/*
	 * From here on, any failure simply returns the error code.
	 * The outer context calling vm_machine_destroy() will safely unwind 
	 * everything previously allocated or added to machm_pool.
	 */

	ret = vm_mem_region_add(&machine->hv.mem_space,
				machine->config.ram_base,
				machine->config.ram_size,
				VM_MEM_FLAG_READONLY | VM_MEM_FLAG_EXEC);
	if (ret < 0) {
		pr_err("failed to map primary system memory\n");
		return ret;
	}

	if (machine->config.machine_class &&
	    machine->config.machine_class->ops &&
	    machine->config.machine_class->ops->init) {
		ret = machine->config.machine_class->ops->init(machine);
		if (ret < 0) {
			pr_err("machine class failed to initialize peripherals\n");
			return ret;
		}
	}

	machine->vcpus = vm_machm_zalloc(
		machine, machine->config.nr_vcpus * sizeof(struct vm_vcpu *));
	machine->vcpu_threads = vm_machm_zalloc(
		machine, machine->config.nr_vcpus * sizeof(struct os_thread *));
	if (!machine->vcpus || !machine->vcpu_threads)
		return -ENOMEM;

	for (i = 0; i < machine->config.nr_vcpus; i++) {
		machine->vcpus[i] =
			vm_machm_zalloc(machine, sizeof(struct vm_vcpu));
		if (!machine->vcpus[i])
			return -ENOMEM;

		ret = vm_vcpu_init(machine->vcpus[i], &machine->hv, i);
		if (ret < 0) {
			pr_err("failed to instantiate vcpu %u\n", i);
			return ret;
		}

		ret = vm_machm_add_action(machine,
					  (void (*)(void *))vm_vcpu_destroy,
					  machine->vcpus[i]);
		if (ret < 0) {
			vm_vcpu_destroy(machine->vcpus[i]);
			return ret;
		}
	}

	pr_info("machine assembled with %zu bytes ram and %u vcpus\n",
		machine->config.ram_size, machine->config.nr_vcpus);

	return 0;
}

/**
 * vm_machine_run - ignite the processor cores and start the main loop.
 * @machine: the fully initialized machine object.
 *
 * Return: 0 upon graceful shutdown, or a negative error code.
 */
int vm_machine_run(struct vm_machine *machine)
{
	unsigned int i;

	if (WARN_ON(!machine))
		return -EINVAL;

	pr_info("powering on the virtual machine...\n");

	os_thread_system_init();

	atomic_store(&machine->hv.is_running, true);

	if (machine->config.machine_class &&
	    machine->config.machine_class->ops &&
	    machine->config.machine_class->ops->reset) {
		int ret = machine->config.machine_class->ops->reset(machine);
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
			machine->vcpu_threads[i] = NULL;
			/* We continue so previously spawned threads can be joined later */
			continue;
		}

		/* Delegate thread handle destruction to the machine pool */
		vm_machm_add_action(machine,
				    (void (*)(void *))os_thread_destroy,
				    machine->vcpu_threads[i]);
	}

	os_mutex_unlock(machine->hv.init_mutex);

	/*
	 * The primary host thread transforms into the system event dispatcher.
	 * It will block indefinitely, routing peripheral I/O, until a shutdown
	 * request is intercepted.
	 */
	vm_event_loop_run(machine);

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

	vm_event_loop_stop(machine);
}

/**
 * vm_machine_destroy - teardown topology and release host resources.
 * @machine: the machine object to destroy.
 */
void vm_machine_destroy(struct vm_machine *machine)
{
	struct vm_device *dev, *n;

	if (!machine)
		return;

	/*
	 * Teardown Barrier 1: Outer Peripherals (Device Domain)
	 * We strictly dismantle devices first so they unregister from the bus
	 * before we destroy the underlying hypervisor containers or memory.
	 */
	list_for_each_entry_safe(dev, n, &machine->devices, node)
	{
		list_del(&dev->node);

		if (dev->ops && dev->ops->unrealize)
			dev->ops->unrealize(dev);

		vm_device_put(dev);
	}

	/*
	 * Teardown Barrier 2: Core Components (Machine Domain)
	 * Automagically destroys thread handles, vcpus, memory maps, event loops,
	 * and the hypervisor context in the strict reverse order of their allocation.
	 */
	vm_res_release_all(&machine->machm_pool);
}