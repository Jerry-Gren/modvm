/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <modvm/core/virtual_cpu.h>
#include <modvm/core/bus.h>
#include <modvm/utils/log.h>
#include <modvm/os/thread.h>

#include "internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm_virtual_cpu: " fmt

/**
 * vm_virtual_cpu_create - request a hardware virtual processor from KVM
 * @cpu: the core cpu structure to populate
 * @hypervisor: the parent hypervisor container
 * @cpu_id: sequential index or APIC ID
 */
int vm_virtual_cpu_create(struct vm_virtual_cpu *cpu,
			  struct vm_hypervisor *hypervisor, int cpu_id)
{
	struct vm_kvm_virtual_cpu_state *kvm_cpu_state;
	struct vm_kvm_state *kvm_state;
	int return_code;

	if (!cpu || !hypervisor || !hypervisor->hypervisor_private_data)
		return -EINVAL;

	kvm_state = hypervisor->hypervisor_private_data;

	kvm_cpu_state = calloc(1, sizeof(*kvm_cpu_state));
	if (!kvm_cpu_state)
		return -ENOMEM;

	kvm_cpu_state->virtual_cpu_file_descriptor =
		ioctl(kvm_state->virtual_machine_file_descriptor,
		      KVM_CREATE_VCPU, cpu_id);
	if (kvm_cpu_state->virtual_cpu_file_descriptor < 0) {
		pr_err("failed to instantiate hardware virtual processor %d\n",
		       cpu_id);
		return_code = -errno;
		goto error_free_kvm_cpu_state;
	}

	if (cpu_id > 0) {
		struct kvm_mp_state mp_state = {
			.mp_state = KVM_MP_STATE_UNINITIALIZED
		};

		if (ioctl(kvm_cpu_state->virtual_cpu_file_descriptor,
			  KVM_SET_MP_STATE, &mp_state) < 0) {
			pr_err("failed to set architectural power state for ap %d\n",
			       cpu_id);
			return_code = -errno;
			goto error_close_cpu_descriptor;
		}
	}

	kvm_cpu_state->kvm_run_mapping_size_bytes = ioctl(
		kvm_state->kvm_file_descriptor, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (kvm_cpu_state->kvm_run_mapping_size_bytes < 0) {
		pr_err("failed to probe virtual processor mmap size\n");
		return_code = -errno;
		goto error_close_cpu_descriptor;
	}

	kvm_cpu_state->kvm_run_structure =
		mmap(NULL, kvm_cpu_state->kvm_run_mapping_size_bytes,
		     PROT_READ | PROT_WRITE, MAP_SHARED,
		     kvm_cpu_state->virtual_cpu_file_descriptor, 0);
	if (kvm_cpu_state->kvm_run_structure == MAP_FAILED) {
		pr_err("failed to map hypervisor communication window\n");
		return_code = -errno;
		goto error_close_cpu_descriptor;
	}

	cpu->cpu_id = cpu_id;
	cpu->hypervisor = hypervisor;
	cpu->hypervisor_private_data = kvm_cpu_state;

	pr_info("virtual processor %d mapped and initialized\n", cpu_id);
	return 0;

error_close_cpu_descriptor:
	close(kvm_cpu_state->virtual_cpu_file_descriptor);
error_free_kvm_cpu_state:
	free(kvm_cpu_state);
	return return_code;
}

/**
 * vm_virtual_cpu_set_instruction_pointer - configure the CPU reset vector
 * @cpu: the virtual processor
 * @entry_point: the physical address to begin execution
 *
 * return: 0 on success, or a negative error code.
 */
int vm_virtual_cpu_set_instruction_pointer(struct vm_virtual_cpu *cpu,
					   uint64_t entry_point)
{
	if (!cpu || !cpu->hypervisor_private_data)
		return -EINVAL;

	return kvm_arch_vcpu_set_pc(cpu, entry_point);
}

static void handle_memory_mapped_io_exit(struct vm_virtual_cpu *cpu)
{
	struct vm_kvm_virtual_cpu_state *kvm_cpu_state =
		cpu->hypervisor_private_data;
	struct kvm_run *run_structure = kvm_cpu_state->kvm_run_structure;

	uint64_t physical_address = run_structure->mmio.phys_addr;
	uint8_t access_size = run_structure->mmio.len;
	uint8_t *data_pointer = run_structure->mmio.data;
	uint64_t value = 0;

	if (run_structure->mmio.is_write) {
		memcpy(&value, data_pointer, access_size);
		vm_bus_dispatch_write(VM_BUS_SPACE_MEMORY_MAPPED_IO,
				      physical_address, value, access_size);
	} else {
		value = vm_bus_dispatch_read(VM_BUS_SPACE_MEMORY_MAPPED_IO,
					     physical_address, access_size);
		memcpy(data_pointer, &value, access_size);
	}
}

static int
virtual_cpu_setup_kvm_signal_mask(struct vm_kvm_virtual_cpu_state *state)
{
	struct kvm_signal_mask *kvm_mask;
	int ret;

	/* Linux kernel expects exactly an 8-byte signal mask */
	const uint32_t KERNEL_SIGSET_SIZE = 8;

	kvm_mask = malloc(sizeof(*kvm_mask) + KERNEL_SIGSET_SIZE);
	if (!kvm_mask)
		return -ENOMEM;

	kvm_mask->len = KERNEL_SIGSET_SIZE;

	ret = os_thread_fill_wakeup_sigmask(kvm_mask->sigset,
					    KERNEL_SIGSET_SIZE);
	if (ret < 0) {
		free(kvm_mask);
		return ret;
	}

	ret = ioctl(state->virtual_cpu_file_descriptor, KVM_SET_SIGNAL_MASK,
		    kvm_mask);
	free(kvm_mask);

	if (ret < 0) {
		pr_err("failed to inject atomic signal mask to hypervisor: %d\n",
		       errno);
		return -errno;
	}

	return 0;
}

/**
 * vm_virtual_cpu_run - the infinite execution loop of the processor
 * @cpu: the virtual processor to run
 *
 * This routine traps into hardware virtualization. It yields control back
 * to the host userspace (VM Exit) only when an unhandled exception occurs
 * or device I/O is intercepted.
 *
 * return: 0 on clean exit, or a negative error code.
 */
int vm_virtual_cpu_run(struct vm_virtual_cpu *cpu)
{
	struct vm_kvm_virtual_cpu_state *kvm_cpu_state;
	struct kvm_run *run;
	int ret;

	if (!cpu || !cpu->hypervisor_private_data)
		return -EINVAL;

	kvm_cpu_state = cpu->hypervisor_private_data;
	run = kvm_cpu_state->kvm_run_structure;

	pr_info("virtual processor %d entering hypervisor execution loop\n",
		cpu->cpu_id);

	os_thread_block_wakeup_signal();

	ret = virtual_cpu_setup_kvm_signal_mask(kvm_cpu_state);
	if (ret < 0)
		return ret;

	/*
     * Wait for the primary thread to finish spawning all siblings.
     */
	os_mutex_lock(cpu->hypervisor->startup_synchronization_lock);
	os_mutex_unlock(cpu->hypervisor->startup_synchronization_lock);

	for (;;) {
		if (!atomic_load(&cpu->hypervisor->is_running))
			return 0;

		ret = ioctl(kvm_cpu_state->virtual_cpu_file_descriptor, KVM_RUN,
			    0);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;

			pr_err("hypervisor ioctl KVM_RUN failed: %d\n", errno);
			return -errno;
		}

		switch (run->exit_reason) {
		case KVM_EXIT_MMIO:
			handle_memory_mapped_io_exit(cpu);
			break;

		case KVM_EXIT_HLT:
			pr_info("virtual processor %d halted by guest operating system\n",
				cpu->cpu_id);
			return 0;

		case KVM_EXIT_INTERNAL_ERROR:
			pr_err("hypervisor internal error, suberror: %d\n",
			       run->internal.suberror);
			return -EFAULT;

		default:
			/* Delegate architecture-specific exits (like PIO) to the arch hook */
			ret = kvm_arch_vcpu_handle_exit(cpu, run);
			if (ret < 0)
				return ret;
			break;
		}
	}
}

/**
 * vm_virtual_cpu_destroy - safely tear down virtual processor resources
 * @cpu: the virtual processor to destroy
 */
void vm_virtual_cpu_destroy(struct vm_virtual_cpu *cpu)
{
	struct vm_kvm_virtual_cpu_state *kvm_cpu_state;

	if (!cpu || !cpu->hypervisor_private_data)
		return;

	kvm_cpu_state = cpu->hypervisor_private_data;

	munmap(kvm_cpu_state->kvm_run_structure,
	       kvm_cpu_state->kvm_run_mapping_size_bytes);
	close(kvm_cpu_state->virtual_cpu_file_descriptor);

	free(kvm_cpu_state);
	cpu->hypervisor_private_data = NULL;
}