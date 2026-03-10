/* SPDX-License-Identifier: GPL-2.0 */
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

#include <modvm/core/virtual_cpu.h>
#include <modvm/core/bus.h>
#include <modvm/utils/log.h>

#include "../internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm_x86: " fmt

/**
 * kvm_arch_vcpu_set_pc - set the instruction pointer for x86_64
 * @cpu: the virtual processor
 * @pc: the physical address to begin execution
 *
 * Configures the code segment and instruction pointer registers
 * to prepare the vCPU for 16-bit real mode execution at the given
 * physical address.
 *
 * return: 0 on success, or a negative error code.
 */
int kvm_arch_vcpu_set_pc(struct vm_virtual_cpu *cpu, uint64_t pc)
{
	struct vm_kvm_virtual_cpu_state *state = cpu->hypervisor_private_data;
	struct kvm_sregs sregs;
	struct kvm_regs regs;

	if (ioctl(state->virtual_cpu_file_descriptor, KVM_GET_SREGS, &sregs) <
	    0)
		return -errno;

	sregs.cs.selector = 0;
	sregs.cs.base = 0;
	if (ioctl(state->virtual_cpu_file_descriptor, KVM_SET_SREGS, &sregs) <
	    0)
		return -errno;

	memset(&regs, 0, sizeof(regs));
	regs.rip = pc;
	/* RFLAGS bit 1 must always be 1 according to the x86 architecture manual */
	regs.rflags = 0x2;

	if (ioctl(state->virtual_cpu_file_descriptor, KVM_SET_REGS, &regs) < 0)
		return -errno;

	return 0;
}

/**
 * kvm_arch_vcpu_handle_exit - handle architecture-specific KVM exits
 * @cpu: the virtual processor
 * @run: the kvm_run structure containing exit state
 *
 * Handles x86-specific exits such as Port I/O and triple faults.
 *
 * return: 0 if the exit was handled and execution should continue,
 * or a negative error code to abort execution.
 */
int kvm_arch_vcpu_handle_exit(struct vm_virtual_cpu *cpu, struct kvm_run *run)
{
	uint16_t port;
	uint8_t size;
	uint32_t count, i;
	uint8_t *data;

	switch (run->exit_reason) {
	case KVM_EXIT_IO:
		port = run->io.port;
		size = run->io.size;
		count = run->io.count;
		data = (uint8_t *)run + run->io.data_offset;

		for (i = 0; i < count; i++) {
			if (run->io.direction == KVM_EXIT_IO_OUT) {
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

				vm_bus_dispatch_write(VM_BUS_SPACE_PORT_IO,
						      port, val, size);
			} else {
				uint64_t val = vm_bus_dispatch_read(
					VM_BUS_SPACE_PORT_IO, port, size);

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
		pr_err("virtual processor %d triggered a fatal triple fault\n",
		       cpu->cpu_id);
		return -EFAULT;

	default:
		pr_warn("unhandled architecture-specific hypervisor exit: %d\n",
			run->exit_reason);
		return -ENOTSUP;
	}
}