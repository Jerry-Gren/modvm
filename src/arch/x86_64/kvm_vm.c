/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <modvm/vm.h>
#include <modvm/log.h>
#include <modvm/err.h>
#include <modvm/bug.h>

#include "kvm_internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm_vm: " fmt

/**
 * kvm_mem_map_cb - bridge between core memory allocator and KVM EPT
 *
 * This callback is invoked whenever the core adds a new memory region.
 * It issues the KVM_SET_USER_MEMORY_REGION ioctl to bind the host virtual
 * address to the guest physical address in the hardware page tables.
 */
static int kvm_mem_map_cb(struct vm_memory_space *space,
			  struct vm_memory_region *region, void *opaque)
{
	struct arch_vm *arch = opaque;
	struct kvm_userspace_memory_region kvm_region = {
		.slot = arch->mem_slot_index++,
		.guest_phys_addr = region->gpa,
		.memory_size = region->size,
		.userspace_addr = (uint64_t)region->hva,
		.flags = 0,
	};

	(void)space;

	if (region->flags & VM_MEM_F_READONLY)
		kvm_region.flags |= KVM_MEM_READONLY;

	if (ioctl(arch->vm_fd, KVM_SET_USER_MEMORY_REGION, &kvm_region) < 0) {
		pr_err("KVM failed to set user memory region: %d\n", errno);
		return -errno;
	}

	return 0;
}

int vm_create(struct vm *vm)
{
	struct arch_vm *arch;
	int ret;

	if (!vm)
		return -EINVAL;

	arch = calloc(1, sizeof(*arch));
	if (!arch)
		return -ENOMEM;

	arch->kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
	if (arch->kvm_fd < 0) {
		pr_err("Failed to open /dev/kvm. Are the kvm modules loaded?\n");
		ret = -errno;
		goto err_free_arch;
	}

	ret = ioctl(arch->kvm_fd, KVM_GET_API_VERSION, 0);
	if (ret != KVM_API_VERSION) {
		pr_err("Unsupported KVM API version: %d\n", ret);
		ret = -ENOTSUP;
		goto err_close_kvm;
	}

	arch->vm_fd = ioctl(arch->kvm_fd, KVM_CREATE_VM, 0);
	if (arch->vm_fd < 0) {
		pr_err("Failed to create KVM virtual machine\n");
		ret = -errno;
		goto err_close_kvm;
	}

	arch->mem_slot_index = 0;
	vm->arch = arch;

	ret = vm_memory_space_init(&vm->mem_space, kvm_mem_map_cb, arch);
	if (ret < 0) {
		pr_err("Failed to initialize core memory space\n");
		goto err_close_vm;
	}

	/*
	 * Initialize the power state flag. The machine will explicitly
         * assert this flag when it is ready to ungate the vCPUs.
         */
	atomic_init(&vm->running, false);

	pr_info("KVM virtual machine created successfully\n");
	return 0;

err_close_vm:
	close(arch->vm_fd);
err_close_kvm:
	close(arch->kvm_fd);
err_free_arch:
	free(arch);
	return ret;
}

int vm_arch_setup_irqchip(struct vm *vm)
{
	struct kvm_pit_config pit_config = { .flags = 0 };

	if (!vm || !vm->arch)
		return -EINVAL;

	/*
	 * Instantiate the virtual Programmable Interrupt Controller (PIC),
	 * the IOAPIC, and the Local APICs for each vCPU.
	 */
	if (ioctl(vm->arch->vm_fd, KVM_CREATE_IRQCHIP, 0) < 0) {
		pr_err("Failed to create in-kernel IRQ chip: %d\n", errno);
		return -errno;
	}

	/*
	 * The Programmable Interval Timer (PIT) provides the legacy
	 * system tick (IRQ 0) which is critical for guest OS scheduling.
	 */
	if (ioctl(vm->arch->vm_fd, KVM_CREATE_PIT2, &pit_config) < 0) {
		pr_err("Failed to create in-kernel PIT: %d\n", errno);
		return -errno;
	}

	pr_info("Architectural interrupt controllers instantiated\n");
	return 0;
}

/**
 * vm_set_irq - inject a hardware interrupt into the KVM guest
 * @vm: the virtual machine container
 * @irq: the hardware interrupt line number
 * @level: 1 to assert the interrupt, 0 to deassert
 *
 * Translates the core architecture-agnostic interrupt request into
 * the specific KVM_IRQ_LINE ioctl required by the Linux kernel.
 */
int vm_set_irq(struct vm *vm, uint32_t irq, int level)
{
	struct kvm_irq_level irq_level;

	if (WARN_ON(!vm || !vm->arch))
		return -EINVAL;

	irq_level.irq = irq;
	irq_level.level = level;

	if (ioctl(vm->arch->vm_fd, KVM_IRQ_LINE, &irq_level) < 0) {
		pr_err("KVM failed to inject IRQ %u\n", irq);
		return -errno;
	}

	return 0;
}

void vm_destroy(struct vm *vm)
{
	if (!vm || !vm->arch)
		return;

	vm_memory_space_destroy(&vm->mem_space);
	close(vm->arch->vm_fd);
	close(vm->arch->kvm_fd);
	free(vm->arch);
	vm->arch = NULL;
}