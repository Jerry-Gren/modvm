/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <errno.h>

#include <modvm/machine.h>
#include <modvm/log.h>
#include <modvm/err.h>
#include <modvm/bug.h>
#include <modvm/os_thread.h>
#include <modvm/event_loop.h>

#undef pr_fmt
#define pr_fmt(fmt) "machine: " fmt

/**
 * vcpu_thread_entry - the thread entry point for each vCPU
 * @opaque: pointer to the specific vcpu structure
 *
 * This routine acts as the heartbeat for a single processor core, trapping
 * continuously in and out of the hardware virtualization extensions.
 *
 * Return: NULL upon thread exit.
 */
static void *vcpu_thread_entry(void *opaque)
{
	struct vcpu *vcpu = opaque;
	int ret;

	ret = vcpu_run(vcpu);
	if (ret < 0)
		pr_err("Fatal execution error on vCPU %d\n", vcpu->id);

	return NULL;
}

int machine_init(struct machine *mach, const struct machine_config *cfg)
{
	unsigned int i;
	int ret;

	if (WARN_ON(!mach || !cfg))
		return -EINVAL;

	mach->config = *cfg;

	/* Bring up the hypervisor container first to establish the VM boundary */
	ret = vm_create(&mach->vm);
	if (ret < 0) {
		pr_err("Failed to create hypervisor container\n");
		return ret;
	}

	/* Dynamically allocate the primary RAM bank based on configuration */
	ret = vm_memory_region_add(&mach->vm.mem_space, mach->config.ram_base,
				   mach->config.ram_size,
				   VM_MEM_F_READONLY | VM_MEM_F_EXEC);
	if (ret < 0) {
		pr_err("Failed to allocate primary system RAM\n");
		goto err_destroy_vm;
	}

	/*
	 * Inversion of Control: Defer peripheral initialization to the
	 * provided callback. This prevents architecture leak into the core.
	 */
	if (mach->config.machine_type && mach->config.machine_type->init) {
		ret = mach->config.machine_type->init(mach);
		if (ret < 0) {
			pr_err("Failed to initialize motherboard peripherals\n");
			goto err_destroy_vm;
		}
	}

	mach->vcpus = calloc(mach->config.smp_cpus, sizeof(struct vcpu *));
	mach->threads =
		calloc(mach->config.smp_cpus, sizeof(struct os_thread *));
	if (!mach->vcpus || !mach->threads) {
		ret = -ENOMEM;
		goto err_free_topology;
	}

	for (i = 0; i < mach->config.smp_cpus; i++) {
		mach->vcpus[i] = calloc(1, sizeof(struct vcpu));
		if (!mach->vcpus[i]) {
			ret = -ENOMEM;
			goto err_free_vcpus;
		}

		ret = vcpu_create(mach->vcpus[i], &mach->vm, i);
		if (ret < 0) {
			pr_err("Failed to instantiate vCPU %u\n", i);
			free(mach->vcpus[i]);
			mach->vcpus[i] = NULL;
			goto err_free_vcpus;
		}
	}

	pr_info("Machine initialized with %zu bytes RAM and %u vCPUs\n",
		mach->config.ram_size, mach->config.smp_cpus);

	return 0;

err_free_vcpus:
	while (i > 0) {
		--i;
		if (mach->vcpus[i]) {
			vcpu_destroy(mach->vcpus[i]);
			free(mach->vcpus[i]);
		}
	}
err_free_topology:
	free(mach->vcpus);
	free(mach->threads);
err_destroy_vm:
	vm_destroy(&mach->vm);
	return ret;
}

int machine_run(struct machine *mach)
{
	unsigned int i;
	int ret;

	if (WARN_ON(!mach))
		return -EINVAL;

	pr_info("Powering on the virtual machine...\n");

	/* Initialize the asynchronous event dispatch core */
	ret = event_loop_init();
	if (ret < 0)
		return ret;

	os_thread_system_init();

	/* Energize the core virtual machine container */
	atomic_store(&mach->vm.running, true);

	/*
	 * Delegate the architecture-specific reset protocol to the machine class.
	 * This handles loading the boot image into the correct physical memory
	 * address and configuring the Bootstrap Processor's registers.
	 */
	if (mach->config.machine_type && mach->config.machine_type->reset) {
		ret = mach->config.machine_type->reset(mach);
		if (ret < 0) {
			pr_err("Machine reset and boot preparation failed\n");
			return ret;
		}
	}

	for (i = 0; i < mach->config.smp_cpus; i++) {
		/* Directly pass the vcpu instance, maintaining 1:1 conceptual mapping */
		mach->threads[i] =
			os_thread_create(vcpu_thread_entry, mach->vcpus[i]);
		if (IS_ERR(mach->threads[i])) {
			pr_err("Failed to spawn thread for vCPU %u\n", i);
			return PTR_ERR(mach->threads[i]);
		}
	}

	/*
	 * Hand over the primary control plane thread to the event loop.
	 * The thread will park here safely, routing I/O to virtual devices,
	 * until a guest shutdown request breaks the loop.
	 */
	event_loop_run();

	/*
	 * The event loop has terminated. The system is entering a power-off state.
	 * We safely reap all virtual processor threads before returning.
	 */
	for (i = 0; i < mach->config.smp_cpus; i++) {
		if (mach->threads[i]) {
			os_thread_join(mach->threads[i]);
		}
	}

	pr_info("Machine powered off gracefully\n");
	return 0;
}

void machine_request_shutdown(struct machine *mach)
{
	unsigned int i;

	if (!mach)
		return;

	/* Drop the global power flag */
	atomic_store(&mach->vm.running, false);

	/* Broadcast hardware wake-up signal to all processor threads */
	for (i = 0; i < mach->config.smp_cpus; i++) {
		if (mach->threads[i])
			os_thread_kick(mach->threads[i]);
	}

	/* Release the primary thread from the blocking dispatch loop */
	event_loop_stop();
}

void machine_destroy(struct machine *mach)
{
	unsigned int i;

	if (!mach)
		return;

	/* Safely dismantle the virtual CPU topology */
	if (mach->vcpus) {
		for (i = 0; i < mach->config.smp_cpus; i++) {
			if (mach->vcpus[i]) {
				vcpu_destroy(mach->vcpus[i]);
				free(mach->vcpus[i]);
			}
		}
		free(mach->vcpus);
		mach->vcpus = NULL;
	}

	/* Safely tear down the OS thread tracking structures */
	if (mach->threads) {
		for (i = 0; i < mach->config.smp_cpus; i++) {
			if (mach->threads[i]) {
				os_thread_destroy(mach->threads[i]);
			}
		}
		free(mach->threads);
		mach->threads = NULL;
	}

	vm_destroy(&mach->vm);
}