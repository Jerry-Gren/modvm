/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include <modvm/vcpu.h>
#include <modvm/vm.h>
#include <modvm/bus.h>
#include <modvm/log.h>
#include <modvm/os_thread.h>

#include "kvm_internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm_vcpu: " fmt

int vcpu_create(struct vcpu *vcpu, struct vm *vm, int id)
{
	struct arch_vcpu *arch;
	int ret;

	if (!vcpu || !vm || !vm->arch)
		return -EINVAL;

	arch = calloc(1, sizeof(*arch));
	if (!arch)
		return -ENOMEM;

	arch->vcpu_fd = ioctl(vm->arch->vm_fd, KVM_CREATE_VCPU, id);
	if (arch->vcpu_fd < 0) {
		pr_err("Failed to create KVM vCPU %d\n", id);
		ret = -errno;
		goto err_free_arch;
	}

	/*
	 * Multiprocessor initialization protocol.
	 * The Bootstrap Processor (vCPU 0) naturally boots into the executable
	 * state. All Application Processors (vCPU > 0) must be halted in an
	 * uninitialized state until the BSP explicitly awakens them via the
	 * Local APIC using the INIT-SIPI sequence.
	 */
	if (id > 0) {
		struct kvm_mp_state mp_state = {
			.mp_state = KVM_MP_STATE_UNINITIALIZED
		};

		if (ioctl(arch->vcpu_fd, KVM_SET_MP_STATE, &mp_state) < 0) {
			pr_err("Failed to set MP state for AP vCPU %d\n", id);
			ret = -errno;
			goto err_close_vcpu;
		}
	}

	arch->run_sz = ioctl(vm->arch->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (arch->run_sz < 0) {
		pr_err("Failed to get vCPU mmap size\n");
		ret = -errno;
		goto err_close_vcpu;
	}

	arch->kvm_run = mmap(NULL, arch->run_sz, PROT_READ | PROT_WRITE,
			     MAP_SHARED, arch->vcpu_fd, 0);
	if (arch->kvm_run == MAP_FAILED) {
		pr_err("Failed to mmap kvm_run structure\n");
		ret = -errno;
		goto err_close_vcpu;
	}

	vcpu->id = id;
	vcpu->vm = vm;
	vcpu->arch = arch;

	pr_info("vCPU %d created and mapped successfully\n", id);
	return 0;

err_close_vcpu:
	close(arch->vcpu_fd);
err_free_arch:
	free(arch);
	return ret;
}

int vcpu_set_pc(struct vcpu *vcpu, uint64_t entry_point)
{
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	if (!vcpu || !vcpu->arch)
		return -EINVAL;

	/*
	 * In x86, we must configure the segment registers (CS) to ensure
	 * the CPU evaluates the physical address correctly in Real Mode.
	 */
	if (ioctl(vcpu->arch->vcpu_fd, KVM_GET_SREGS, &sregs) < 0)
		return -errno;

	/* Setup a flat 16-bit real mode memory model */
	sregs.cs.selector = 0;
	sregs.cs.base = 0;
	if (ioctl(vcpu->arch->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
		return -errno;

	/* Clear standard registers and set the Instruction Pointer (RIP) */
	memset(&regs, 0, sizeof(regs));
	regs.rip = entry_point;
	/* RFLAGS bit 1 must always be 1 according to the x86 architecture manual */
	regs.rflags = 0x2;

	if (ioctl(vcpu->arch->vcpu_fd, KVM_SET_REGS, &regs) < 0)
		return -errno;

	return 0;
}

/**
 * handle_io_exit - dispatch hardware Port I/O instructions to the emulated bus
 * @vcpu: the vcpu generating the trap
 *
 * This is called when an x86 guest executes an IN or OUT instruction.
 * We extract the payload and width, and route it to the PIO address space.
 */
static void handle_io_exit(struct vcpu *vcpu)
{
	struct kvm_run *run = vcpu->arch->kvm_run;
	uint16_t port = run->io.port;
	uint8_t size = run->io.size;
	uint32_t count = run->io.count;
	uint8_t *data_ptr = (uint8_t *)run + run->io.data_offset;
	uint32_t i;

	/*
	 * Hardware string I/O (like 'rep outsb') can emit multiple bytes
	 * in a single exit trap. We must iterate over the repetition count.
	 */
	for (i = 0; i < count; i++) {
		if (run->io.direction == KVM_EXIT_IO_OUT) {
			uint64_t value = 0;

			switch (size) {
			case 1:
				value = *(uint8_t *)data_ptr;
				break;
			case 2:
				value = *(uint16_t *)data_ptr;
				break;
			case 4:
				value = *(uint32_t *)data_ptr;
				break;
			}

			bus_dispatch_write(VM_BUS_SPACE_PIO, port, value, size);
		} else {
			uint64_t value;

			value = bus_dispatch_read(VM_BUS_SPACE_PIO, port, size);

			switch (size) {
			case 1:
				*(uint8_t *)data_ptr = (uint8_t)value;
				break;
			case 2:
				*(uint16_t *)data_ptr = (uint16_t)value;
				break;
			case 4:
				*(uint32_t *)data_ptr = (uint32_t)value;
				break;
			}
		}
		data_ptr += size;
	}
}

/**
 * handle_mmio_exit - dispatch Memory-Mapped I/O instructions
 * @vcpu: the vcpu generating the trap
 *
 * This catches memory access faults outside of the standard RAM regions.
 * We route these requests to the MMIO address space. This is critical
 * for modern hardware architectures like ARM64 or PCIe devices on x86.
 */
static void handle_mmio_exit(struct vcpu *vcpu)
{
	struct kvm_run *run = vcpu->arch->kvm_run;
	uint64_t addr = run->mmio.phys_addr;
	uint8_t len = run->mmio.len;
	uint8_t *data_ptr = run->mmio.data;
	uint64_t value = 0;

	if (run->mmio.is_write) {
		/*
		 * KVM exposes MMIO payload data as a byte array to handle
		 * arbitrary endianness cleanly.
		 */
		memcpy(&value, data_ptr, len);
		bus_dispatch_write(VM_BUS_SPACE_MMIO, addr, value, len);
	} else {
		value = bus_dispatch_read(VM_BUS_SPACE_MMIO, addr, len);
		memcpy(data_ptr, &value, len);
	}
}

/**
 * vcpu_setup_kvm_sigmask - configure atomic signal unblocking for guest mode
 * @arch: the architecture-specific vCPU context
 *
 * Prepares a signal mask that unblocks the kick signal, passing it to the
 * KVM subsystem. KVM applies this mask atomically during VMX/SVM transition,
 * guaranteeing safe interruption without race windows.
 */
static int vcpu_setup_kvm_sigmask(struct arch_vcpu *arch)
{
	struct kvm_signal_mask *kvm_mask;
	sigset_t unblocked_set;
	int kick_sig;
	int ret;

	/*
         * Userspace glibc defines sigset_t as 128 bytes to support up to
         * 1024 signals. However, the Linux kernel expects exactly 8 bytes
         * (64 bits) for the KVM_SET_SIGNAL_MASK ioctl. Passing 128 causes
         * the kernel to reject the payload with EINVAL.
         */
	const uint32_t KERNEL_SIGSET_SIZE = 8;

	kick_sig = os_thread_get_kick_signal();
	if (kick_sig == -1)
		return -EINVAL;

	/* Obtain the current userspace thread's signal mask */
	pthread_sigmask(SIG_SETMASK, NULL, &unblocked_set);

	/* Unblock the dynamic kick signal exclusively for the hardware context */
	sigdelset(&unblocked_set, kick_sig);

	kvm_mask = malloc(sizeof(*kvm_mask) + KERNEL_SIGSET_SIZE);
	if (!kvm_mask)
		return -ENOMEM;

	kvm_mask->len = KERNEL_SIGSET_SIZE;
	/*
	 * Copy only the first 64 bits. SIGUSR1 (signal 10) falls
         * well within this range.
         */
	memcpy(kvm_mask->sigset, &unblocked_set, KERNEL_SIGSET_SIZE);

	ret = ioctl(arch->vcpu_fd, KVM_SET_SIGNAL_MASK, kvm_mask);
	free(kvm_mask);

	if (ret < 0) {
		pr_err("Failed to configure atomic signal mask for KVM: %d\n",
		       errno);
		return -errno;
	}

	return 0;
}

int vcpu_run(struct vcpu *vcpu)
{
	struct arch_vcpu *arch;
	struct kvm_run *run;
	int ret;

	if (!vcpu || !vcpu->arch)
		return -EINVAL;

	arch = vcpu->arch;
	run = arch->kvm_run;

	pr_info("Entering vCPU %d execution loop...\n", vcpu->id);

	/*
         * Block the kick signal in userspace immediately upon thread entry.
         * This closes the critical race window prior to the KVM_RUN ioctl.
         */
	os_thread_mask_kick_signal();

	/* Delegate the unblocking responsibility to the hardware backend */
	ret = vcpu_setup_kvm_sigmask(arch);
	if (ret < 0)
		return ret;

	for (;;) {
		if (!atomic_load(&vcpu->vm->running))
			return 0;

		ret = ioctl(arch->vcpu_fd, KVM_RUN, 0);
		if (ret < 0) {
			/*
			 * When KVM_RUN is interrupted by our unblocked SIGUSR1,
                         * it returns with EINTR. The loop will safely reiterate
                         * and evaluate the 'running' atomic flag.
                         */
			if (errno == EINTR || errno == EAGAIN)
				continue;

			pr_err("KVM_RUN failed: %d\n", errno);
			return -errno;
		}

		switch (run->exit_reason) {
		case KVM_EXIT_IO:
			handle_io_exit(vcpu);
			break;

		case KVM_EXIT_MMIO:
			handle_mmio_exit(vcpu);
			break;

		case KVM_EXIT_HLT:
			pr_info("vCPU %d halted by guest OS\n", vcpu->id);
			return 0;

		case KVM_EXIT_SHUTDOWN:
			pr_err("vCPU %d triggered a triple fault shutdown\n",
			       vcpu->id);
			return -EFAULT;

		case KVM_EXIT_INTERNAL_ERROR:
			pr_err("KVM internal error. Suberror: %d\n",
			       run->internal.suberror);
			return -EFAULT;

		default:
			pr_warn("Unhandled KVM exit reason: %d\n",
				run->exit_reason);
			return -ENOTSUP;
		}
	}
}

void vcpu_destroy(struct vcpu *vcpu)
{
	if (!vcpu || !vcpu->arch)
		return;

	munmap(vcpu->arch->kvm_run, vcpu->arch->run_sz);
	close(vcpu->arch->vcpu_fd);
	free(vcpu->arch);
	vcpu->arch = NULL;
}