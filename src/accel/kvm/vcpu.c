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
#include <modvm/core/modvm.h>
#include <modvm/utils/log.h>
#include <modvm/os/thread.h>
#include <modvm/utils/container_of.h>
#include <modvm/utils/compiler.h>

#include <modvm/internal/thread.h>

#include "internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm_vcpu: " fmt

/**
 * kvm_vcpu_init - request a hardware virtual processor from the host kernel
 * @vcpu: the core vcpu structure to populate
 *
 * Return: 0 on success, or a negative error code.
 */
static int kvm_vcpu_init(struct modvm_vcpu *vcpu)
{
	struct modvm_kvm_vcpu_state *vcpu_state;
	struct modvm_kvm_state *state = vcpu->accel->priv;
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

	/* Assign early so architecture hooks can access the specific state */
	vcpu->priv = vcpu_state;

	ret = modvm_kvm_arch_vcpu_init(vcpu);
	if (ret < 0)
		goto err_close_fd;

	vcpu_state->run_size = ioctl(state->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (vcpu_state->run_size < 0) {
		pr_err("failed to probe vcpu mmap size\n");
		ret = -errno;
		goto err_close_fd;
	}

	vcpu_state->run = mmap(NULL, (size_t)vcpu_state->run_size,
			       PROT_READ | PROT_WRITE, MAP_SHARED,
			       vcpu_state->vcpu_fd, 0);
	if (vcpu_state->run == MAP_FAILED) {
		pr_err("failed to map hypervisor communication window\n");
		ret = -errno;
		goto err_close_fd;
	}

	pr_info("vcpu %d mapped and initialized\n", vcpu->id);
	return 0;

err_close_fd:
	close(vcpu_state->vcpu_fd);
err_free_state:
	free(vcpu_state);
	vcpu->priv = NULL;
	return ret;
}

static int kvm_vcpu_get_regs_wrap(struct modvm_vcpu *vcpu,
				  enum modvm_reg_class reg_class, void *buf,
				  size_t size)
{
	return modvm_kvm_arch_vcpu_get_regs(vcpu, reg_class, buf, size);
}

static int kvm_vcpu_set_regs_wrap(struct modvm_vcpu *vcpu,
				  enum modvm_reg_class reg_class,
				  const void *buf, size_t size)
{
	return modvm_kvm_arch_vcpu_set_regs(vcpu, reg_class, buf, size);
}

/**
 * handle_mmio_exit - route memory-mapped IO traps to the system bus
 * @vcpu: the trapped virtual processor
 */
static void handle_mmio_exit(struct modvm_vcpu *vcpu)
{
	struct modvm_kvm_vcpu_state *state = vcpu->priv;
	struct kvm_run *run = state->run;
	struct modvm_ctx *ctx =
		container_of(vcpu->accel, struct modvm_ctx, accel);

	uint64_t gpa = run->mmio.phys_addr;
	uint8_t size = run->mmio.len;
	uint8_t *data = run->mmio.data;
	uint64_t val = 0;

	if (run->mmio.is_write) {
		memcpy(&val, data, size);
		modvm_bus_dispatch_write(ctx, MODVM_BUS_MMIO, gpa, val, size);
	} else {
		val = modvm_bus_dispatch_read(ctx, MODVM_BUS_MMIO, gpa, size);
		memcpy(data, &val, size);
	}
}

static int setup_kvm_sigmask(struct modvm_kvm_vcpu_state *state)
{
	struct kvm_signal_mask *kvm_mask;
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
 * kvm_vcpu_run - enter the execution loop of the hardware processor
 * @vcpu: the virtual processor to run
 *
 * Return: 0 on successful exit, or a negative error code.
 */
static int kvm_vcpu_run(struct modvm_vcpu *vcpu)
{
	struct modvm_kvm_vcpu_state *state = vcpu->priv;
	struct kvm_run *run = state->run;
	int ret;

	pr_info("vcpu %d transitioning into hardware execution\n", vcpu->id);

	os_thread_block_wakeup();

	ret = setup_kvm_sigmask(state);
	if (ret < 0)
		return ret;

	os_mutex_lock(vcpu->accel->init_mutex);
	os_mutex_unlock(vcpu->accel->init_mutex);

	for (;;) {
		if (unlikely(!atomic_load(&vcpu->accel->is_running)))
			return 0;

		ret = ioctl(state->vcpu_fd, KVM_RUN, 0);
		if (unlikely(ret < 0)) {
			if (likely(errno == EINTR || errno == EAGAIN))
				continue;

			pr_err("KVM_RUN ioctl critically failed: %d\n", errno);
			return -errno;
		}

		switch (run->exit_reason) {
		case KVM_EXIT_MMIO:
			handle_mmio_exit(vcpu);
			break;

		case KVM_EXIT_HLT:
			pr_info("vcpu %d received halt instruction\n",
				vcpu->id);
			return 0;

		case KVM_EXIT_INTERNAL_ERROR:
			pr_err("hypervisor internal error, suberror code: %d\n",
			       run->internal.suberror);
			return -EFAULT;

		default:
			ret = modvm_kvm_arch_vcpu_handle_exit(vcpu, run);
			if (unlikely(ret < 0))
				return ret;
			break;
		}
	}
}

/**
 * kvm_vcpu_destroy - safely unmap and release hardware vCPU allocations
 * @vcpu: the virtual processor to destroy
 */
static void kvm_vcpu_destroy(struct modvm_vcpu *vcpu)
{
	struct modvm_kvm_vcpu_state *state = vcpu->priv;

	if (!state)
		return;

	if (state->run)
		munmap(state->run, (size_t)state->run_size);
	if (state->vcpu_fd >= 0)
		close(state->vcpu_fd);

	free(state);
}

const struct modvm_vcpu_ops modvm_kvm_vcpu_ops = {
	.init = kvm_vcpu_init,
	.destroy = kvm_vcpu_destroy,
	.get_regs = kvm_vcpu_get_regs_wrap,
	.set_regs = kvm_vcpu_set_regs_wrap,
	.run = kvm_vcpu_run,
};