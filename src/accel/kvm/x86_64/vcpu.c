/* SPDX-License-Identifier: GPL-2.0 */
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <modvm/core/vcpu.h>
#include <modvm/core/bus.h>
#include <modvm/core/modvm.h>
#include <modvm/internal/arch/x86/regs.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>

#include "../internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm_x86: " fmt

#define MAX_KVM_CPUID_ENTRIES 1024

/**
 * kvm_x86_cpuid_setup - dynamically probe and inject host CPU features
 * @state: the global KVM state containing the hypervisor file descriptor
 * @vcpu_fd: the file descriptor of the target virtual processor
 *
 * Utilizes an exponential backoff allocation strategy to retrieve the
 * full set of supported CPUID leaves from the host kernel, bypassing
 * hardcoded limits that cause failures on modern high-end processors.
 *
 * Return: 0 on success, or a negative error code.
 */
static int kvm_x86_cpuid_setup(struct modvm_kvm_state *state, int vcpu_fd)
{
	struct kvm_cpuid2 *cpuid;
	int nent = 100;
	size_t alloc_size;
	int ret;

	while (nent <= MAX_KVM_CPUID_ENTRIES) {
		alloc_size =
			sizeof(*cpuid) + nent * sizeof(struct kvm_cpuid_entry2);
		cpuid = malloc(alloc_size);
		if (!cpuid)
			return -ENOMEM;

		cpuid->nent = nent;
		ret = ioctl(state->kvm_fd, KVM_GET_SUPPORTED_CPUID, cpuid);
		if (ret == 0)
			break;

		ret = -errno;
		free(cpuid);

		/* Array too small, scale up exponentially and retry */
		if (ret == -E2BIG) {
			nent *= 2;
			continue;
		}

		pr_err("failed to probe supported cpuid topology: %d\n", ret);
		return ret;
	}

	if (nent > MAX_KVM_CPUID_ENTRIES) {
		pr_err("kvm cpuid entries exceeded safe capacity limit\n");
		return -E2BIG;
	}

	ret = ioctl(vcpu_fd, KVM_SET_CPUID2, cpuid);
	if (ret < 0) {
		ret = -errno;
		pr_err("failed to inject cpuid definitions into vcpu: %d\n",
		       ret);
	}

	free(cpuid);
	return ret;
}

/**
 * modvm_kvm_arch_vcpu_init - initialize architecture specific vcpu state
 * @vcpu: the virtual processor to initialize
 *
 * Encapsulates the configuration of legacy PC states such as CPUID matrices
 * and multiprocessor AP initialization required by the Intel/AMD platform.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_kvm_arch_vcpu_init(struct modvm_vcpu *vcpu)
{
	struct modvm_kvm_vcpu_state *vcpu_state = vcpu->priv;
	struct modvm_kvm_state *state = vcpu->accel->priv;
	int ret;

	ret = kvm_x86_cpuid_setup(state, vcpu_state->vcpu_fd);
	if (ret < 0)
		return ret;

	/*
	 * Application Processors (APs) must be initialized in a specific power state,
	 * waiting for the Bootstrap Processor (BSP) to send SIPI IPIs.
	 */
	if (vcpu->id > 0) {
		struct kvm_mp_state mp_state = {
			.mp_state = KVM_MP_STATE_UNINITIALIZED
		};

		if (ioctl(vcpu_state->vcpu_fd, KVM_SET_MP_STATE, &mp_state) <
		    0) {
			ret = -errno;
			pr_err("failed to set architectural power state for ap %d: %d\n",
			       vcpu->id, ret);
			return ret;
		}
	}

	return 0;
}

static void kvm_x86_segment_pack(struct kvm_segment *dst,
				 const struct modvm_x86_segment *src)
{
	dst->base = src->base;
	dst->limit = src->limit;
	dst->selector = src->selector;
	dst->type = src->type;
	dst->present = src->present;
	dst->dpl = src->dpl;
	dst->db = src->db;
	dst->s = src->s;
	dst->l = src->l;
	dst->g = src->g;
	dst->unusable = src->unusable;
}

static void kvm_x86_segment_unpack(struct modvm_x86_segment *dst,
				   const struct kvm_segment *src)
{
	dst->base = src->base;
	dst->limit = src->limit;
	dst->selector = src->selector;
	dst->type = src->type;
	dst->present = src->present;
	dst->dpl = src->dpl;
	dst->db = src->db;
	dst->s = src->s;
	dst->l = src->l;
	dst->g = src->g;
	dst->unusable = src->unusable;
}

/**
 * modvm_kvm_arch_vcpu_get_regs - fetch x86 architectural state from KVM
 * @vcpu: the virtual processor
 * @reg_class: identifier specifying GPRs or Special Registers
 * @buf: destination buffer
 * @size: expected size of the modvm architectural structure
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_kvm_arch_vcpu_get_regs(struct modvm_vcpu *vcpu,
				 enum modvm_reg_class reg_class, void *buf,
				 size_t size)
{
	struct modvm_kvm_vcpu_state *state = vcpu->priv;

	if (reg_class == MODVM_REG_SREGS) {
		struct kvm_sregs k_sregs;
		struct modvm_x86_sregs *m_sregs = buf;

		if (WARN_ON(size != sizeof(*m_sregs)))
			return -EINVAL;

		if (ioctl(state->vcpu_fd, KVM_GET_SREGS, &k_sregs) < 0)
			return -errno;

		kvm_x86_segment_unpack(&m_sregs->cs, &k_sregs.cs);
		kvm_x86_segment_unpack(&m_sregs->ds, &k_sregs.ds);
		kvm_x86_segment_unpack(&m_sregs->es, &k_sregs.es);
		kvm_x86_segment_unpack(&m_sregs->fs, &k_sregs.fs);
		kvm_x86_segment_unpack(&m_sregs->gs, &k_sregs.gs);
		kvm_x86_segment_unpack(&m_sregs->ss, &k_sregs.ss);
		kvm_x86_segment_unpack(&m_sregs->tr, &k_sregs.tr);
		kvm_x86_segment_unpack(&m_sregs->ldt, &k_sregs.ldt);

		m_sregs->cr0 = k_sregs.cr0;
		m_sregs->cr2 = k_sregs.cr2;
		m_sregs->cr3 = k_sregs.cr3;
		m_sregs->cr4 = k_sregs.cr4;
		m_sregs->cr8 = k_sregs.cr8;
		m_sregs->efer = k_sregs.efer;
		m_sregs->apic_base = k_sregs.apic_base;
		return 0;
	}

	if (reg_class == MODVM_REG_GPR) {
		struct kvm_regs k_regs;
		struct modvm_x86_regs *m_regs = buf;

		if (WARN_ON(size != sizeof(*m_regs)))
			return -EINVAL;

		if (ioctl(state->vcpu_fd, KVM_GET_REGS, &k_regs) < 0)
			return -errno;

		m_regs->rax = k_regs.rax;
		m_regs->rbx = k_regs.rbx;
		m_regs->rcx = k_regs.rcx;
		m_regs->rdx = k_regs.rdx;
		m_regs->rsi = k_regs.rsi;
		m_regs->rdi = k_regs.rdi;
		m_regs->rsp = k_regs.rsp;
		m_regs->rbp = k_regs.rbp;
		m_regs->r8 = k_regs.r8;
		m_regs->r9 = k_regs.r9;
		m_regs->r10 = k_regs.r10;
		m_regs->r11 = k_regs.r11;
		m_regs->r12 = k_regs.r12;
		m_regs->r13 = k_regs.r13;
		m_regs->r14 = k_regs.r14;
		m_regs->r15 = k_regs.r15;
		m_regs->rip = k_regs.rip;
		m_regs->rflags = k_regs.rflags;
		return 0;
	}

	return -ENOTSUP;
}

/**
 * modvm_kvm_arch_vcpu_set_regs - commit x86 architectural state to KVM
 * @vcpu: the virtual processor
 * @reg_class: identifier specifying GPRs or Special Registers
 * @buf: source buffer
 * @size: expected size of the modvm architectural structure
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_kvm_arch_vcpu_set_regs(struct modvm_vcpu *vcpu,
				 enum modvm_reg_class reg_class,
				 const void *buf, size_t size)
{
	struct modvm_kvm_vcpu_state *state = vcpu->priv;

	if (reg_class == MODVM_REG_SREGS) {
		struct kvm_sregs k_sregs;
		const struct modvm_x86_sregs *m_sregs = buf;

		if (WARN_ON(size != sizeof(*m_sregs)))
			return -EINVAL;

		/* Fetch existing state to preserve unmapped fields like interrupt bitmaps */
		if (ioctl(state->vcpu_fd, KVM_GET_SREGS, &k_sregs) < 0)
			return -errno;

		kvm_x86_segment_pack(&k_sregs.cs, &m_sregs->cs);
		kvm_x86_segment_pack(&k_sregs.ds, &m_sregs->ds);
		kvm_x86_segment_pack(&k_sregs.es, &m_sregs->es);
		kvm_x86_segment_pack(&k_sregs.fs, &m_sregs->fs);
		kvm_x86_segment_pack(&k_sregs.gs, &m_sregs->gs);
		kvm_x86_segment_pack(&k_sregs.ss, &m_sregs->ss);
		kvm_x86_segment_pack(&k_sregs.tr, &m_sregs->tr);
		kvm_x86_segment_pack(&k_sregs.ldt, &m_sregs->ldt);

		k_sregs.cr0 = m_sregs->cr0;
		k_sregs.cr2 = m_sregs->cr2;
		k_sregs.cr3 = m_sregs->cr3;
		k_sregs.cr4 = m_sregs->cr4;
		k_sregs.cr8 = m_sregs->cr8;
		k_sregs.efer = m_sregs->efer;
		k_sregs.apic_base = m_sregs->apic_base;

		if (ioctl(state->vcpu_fd, KVM_SET_SREGS, &k_sregs) < 0)
			return -errno;
		return 0;
	}

	if (reg_class == MODVM_REG_GPR) {
		struct kvm_regs k_regs;
		const struct modvm_x86_regs *m_regs = buf;

		if (WARN_ON(size != sizeof(*m_regs)))
			return -EINVAL;

		memset(&k_regs, 0, sizeof(k_regs));
		k_regs.rax = m_regs->rax;
		k_regs.rbx = m_regs->rbx;
		k_regs.rcx = m_regs->rcx;
		k_regs.rdx = m_regs->rdx;
		k_regs.rsi = m_regs->rsi;
		k_regs.rdi = m_regs->rdi;
		k_regs.rsp = m_regs->rsp;
		k_regs.rbp = m_regs->rbp;
		k_regs.r8 = m_regs->r8;
		k_regs.r9 = m_regs->r9;
		k_regs.r10 = m_regs->r10;
		k_regs.r11 = m_regs->r11;
		k_regs.r12 = m_regs->r12;
		k_regs.r13 = m_regs->r13;
		k_regs.r14 = m_regs->r14;
		k_regs.r15 = m_regs->r15;
		k_regs.rip = m_regs->rip;
		k_regs.rflags = m_regs->rflags;

		if (ioctl(state->vcpu_fd, KVM_SET_REGS, &k_regs) < 0)
			return -errno;
		return 0;
	}

	return -ENOTSUP;
}

/**
 * modvm_kvm_arch_vcpu_handle_exit - handle architecture-specific traps
 * @vcpu: the virtual processor
 * @run: the shared kvm_run communication structure
 *
 * Processes x86 legacy Port I/O instructions (IN/OUT) by routing them
 * to the global PIO bus architecture.
 *
 * Return: 0 to resume execution, or a negative error code to abort.
 */
int modvm_kvm_arch_vcpu_handle_exit(struct modvm_vcpu *vcpu,
				    struct kvm_run *run)
{
	struct modvm_bus *bus = vcpu->accel->bus;
	uint16_t port;
	uint8_t size;
	uint32_t count;
	uint32_t i;
	uint8_t *data;

	switch (run->exit_reason) {
	case KVM_EXIT_IO:
		port = run->io.port;
		size = run->io.size;
		count = run->io.count;
		data = (uint8_t *)run + run->io.data_offset;

		for (i = 0; i < count; i++) {
			if (likely(run->io.direction == KVM_EXIT_IO_OUT)) {
				uint64_t val = 0;

				switch (size) {
				case 1:
					val = *(uint8_t *)data;
					break;
				case 2:
					val = *(uint16_t *)data;
					break;
				case 4:
					val = *(uint32_t *)data;
					break;
				}

				modvm_bus_dispatch_write(bus, MODVM_BUS_PIO,
							 port, val, size);
			} else {
				uint64_t val = modvm_bus_dispatch_read(
					bus, MODVM_BUS_PIO, port, size);

				switch (size) {
				case 1:
					*(uint8_t *)data = (uint8_t)val;
					break;
				case 2:
					*(uint16_t *)data = (uint16_t)val;
					break;
				case 4:
					*(uint32_t *)data = (uint32_t)val;
					break;
				}
			}
			data += size;
		}
		return 0;

	case KVM_EXIT_SHUTDOWN:
		pr_err("vcpu %d triggered a fatal triple fault hardware shutdown\n",
		       vcpu->id);
		return -EFAULT;

	default:
		pr_warn("unhandled architectural exit reason: %d\n",
			run->exit_reason);
		return -ENOTSUP;
	}
}