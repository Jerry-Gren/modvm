/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <modvm/core/vcpu.h>
#include <modvm/core/bus.h>
#include <modvm/utils/log.h>
#include <modvm/os/thread.h>

#include "internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm_vcpu: " fmt

/**
 * kvm_vcpu_init - request a hardware virtual processor from KVM.
 * @vcpu: the core vcpu structure to populate.
 * @hv: the parent hypervisor container.
 * @id: sequential index or APIC ID/MPIDR.
 *
 * return: 0 on success, or a negative error code.
 */
static int kvm_vcpu_init(struct vm_vcpu *vcpu)
{
	struct kvm_vcpu_state *vcpu_state;
	struct kvm_state *state = vcpu->hv->priv;
	int ret;

	vcpu_state = calloc(1, sizeof(*vcpu_state));
	if (!vcpu_state)
		return -ENOMEM;

	vcpu_state->vcpu_fd = ioctl(state->vm_fd, KVM_CREATE_VCPU, vcpu->id);
	if (vcpu_state->vcpu_fd < 0) {
		pr_err("failed to instantiate hardware vcpu %d\n", vcpu->id);
		ret = -errno;
		goto err_free_state;
	}

	if (vcpu->id > 0) {
		struct kvm_mp_state mp_state = {
			.mp_state = KVM_MP_STATE_UNINITIALIZED
		};

		if (ioctl(vcpu_state->vcpu_fd, KVM_SET_MP_STATE, &mp_state) <
		    0) {
			pr_err("failed to set architectural power state for ap %d\n",
			       vcpu->id);
			ret = -errno;
			goto err_close_fd;
		}
	}

	vcpu_state->run_size = ioctl(state->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (vcpu_state->run_size < 0) {
		pr_err("failed to probe vcpu mmap size\n");
		ret = -errno;
		goto err_close_fd;
	}

	vcpu_state->run = mmap(NULL, vcpu_state->run_size,
			       PROT_READ | PROT_WRITE, MAP_SHARED,
			       vcpu_state->vcpu_fd, 0);
	if (vcpu_state->run == MAP_FAILED) {
		pr_err("failed to map hypervisor communication window\n");
		ret = -errno;
		goto err_close_fd;
	}

	vcpu->priv = vcpu_state;
	pr_info("vcpu %d mapped and initialized\n", vcpu->id);
	return 0;

err_close_fd:
	close(vcpu_state->vcpu_fd);
err_free_state:
	free(vcpu_state);
	return ret;
}

/**
 * kvm_vcpu_set_pc_wrap - configure the CPU reset vector.
 * @vcpu: the virtual processor.
 * @pc: the physical address to begin execution.
 *
 * return: 0 on success, or a negative error code.
 */
static int kvm_vcpu_set_pc_wrap(struct vm_vcpu *vcpu, uint64_t pc)
{
	return kvm_arch_vcpu_set_pc(vcpu, pc);
}

static void handle_mmio_exit(struct vm_vcpu *vcpu)
{
	struct kvm_vcpu_state *state = vcpu->priv;
	struct kvm_run *run = state->run;

	uint64_t gpa = run->mmio.phys_addr;
	uint8_t size = run->mmio.len;
	uint8_t *data = run->mmio.data;
	uint64_t val = 0;

	if (run->mmio.is_write) {
		memcpy(&val, data, size);
		vm_bus_dispatch_write(VM_BUS_MMIO, gpa, val, size);
	} else {
		val = vm_bus_dispatch_read(VM_BUS_MMIO, gpa, size);
		memcpy(data, &val, size);
	}
}

static int setup_kvm_sigmask(struct kvm_vcpu_state *state)
{
	struct kvm_signal_mask *kvm_mask;
	/* Linux kernel expects exactly an 8-byte signal mask */
	const uint32_t KERNEL_SIGSET_SIZE = 8;
	int ret;

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

	ret = ioctl(state->vcpu_fd, KVM_SET_SIGNAL_MASK, kvm_mask);
	free(kvm_mask);

	if (ret < 0) {
		pr_err("failed to inject atomic signal mask: %d\n", errno);
		return -errno;
	}

	return 0;
}

/**
 * kvm_vcpu_run - the execution loop of the processor.
 * @vcpu: the virtual processor to run.
 *
 * Traps into hardware virtualization. Yields control back to host
 * userspace only on exceptions or device I/O.
 *
 * return: 0 on graceful exit, or a negative error code.
 */
static int kvm_vcpu_run(struct vm_vcpu *vcpu)
{
	struct kvm_vcpu_state *state = vcpu->priv;
	struct kvm_run *run = state->run;
	int ret;

	pr_info("vcpu %d entering hypervisor loop\n", vcpu->id);

	os_thread_block_wakeup();

	ret = setup_kvm_sigmask(state);
	if (ret < 0)
		return ret;

	/*
	 * Wait for the primary thread to finish spawning all siblings.
	 */
	os_mutex_lock(vcpu->hv->init_mutex);
	os_mutex_unlock(vcpu->hv->init_mutex);

	for (;;) {
		if (!atomic_load(&vcpu->hv->is_running))
			return 0;

		ret = ioctl(state->vcpu_fd, KVM_RUN, 0);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;

			pr_err("ioctl KVM_RUN failed: %d\n", errno);
			return -errno;
		}

		switch (run->exit_reason) {
		case KVM_EXIT_MMIO:
			handle_mmio_exit(vcpu);
			break;

		case KVM_EXIT_HLT:
			pr_info("vcpu %d halted by guest OS\n", vcpu->id);
			return 0;

		case KVM_EXIT_INTERNAL_ERROR:
			pr_err("hypervisor internal error, suberror: %d\n",
			       run->internal.suberror);
			return -EFAULT;

		default:
			/* Delegate architecture-specific exits (like PIO) to the arch hook */
			ret = kvm_arch_vcpu_handle_exit(vcpu, run);
			if (ret < 0)
				return ret;
			break;
		}
	}
}

/**
 * kvm_vcpu_destroy - safely tear down virtual processor resources.
 * @vcpu: the virtual processor to destroy.
 */
static void kvm_vcpu_destroy(struct vm_vcpu *vcpu)
{
	struct kvm_vcpu_state *state = vcpu->priv;

	munmap(state->run, state->run_size);
	close(state->vcpu_fd);

	free(state);
}

const struct vm_vcpu_ops kvm_vcpu_ops = {
	.init = kvm_vcpu_init,
	.destroy = kvm_vcpu_destroy,
	.set_pc = kvm_vcpu_set_pc_wrap,
	.run = kvm_vcpu_run,
};