/* SPDX-License-Identifier: GPL-2.0 */
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

#include <modvm/core/vcpu.h>
#include <modvm/core/bus.h>
#include <modvm/core/modvm.h>
#include <modvm/utils/container_of.h>
#include <modvm/utils/log.h>
#include <modvm/utils/compiler.h>

#include "../internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm_x86: " fmt

/**
 * modvm_kvm_arch_vcpu_set_pc - set the instruction pointer for x86_64
 * @vcpu: the virtual processor
 * @pc: the physical address to begin execution
 *
 * Prepares the segment registers to enter 16-bit real mode.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_kvm_arch_vcpu_set_pc(struct modvm_vcpu *vcpu, uint64_t pc)
{
	struct modvm_kvm_vcpu_state *state = vcpu->priv;
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	if (ioctl(state->vcpu_fd, KVM_GET_SREGS, &sregs) < 0)
		return -errno;

	sregs.cs.selector = 0;
	sregs.cs.base = 0;
	if (ioctl(state->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
		return -errno;

	memset(&regs, 0, sizeof(regs));
	regs.rip = pc;
	regs.rflags = 0x2;

	if (ioctl(state->vcpu_fd, KVM_SET_REGS, &regs) < 0)
		return -errno;

	return 0;
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
	struct modvm_ctx *ctx =
		container_of(vcpu->accel, struct modvm_ctx, accel);
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

				modvm_bus_dispatch_write(ctx, MODVM_BUS_PIO,
							 port, val, size);
			} else {
				uint64_t val = modvm_bus_dispatch_read(
					ctx, MODVM_BUS_PIO, port, size);

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