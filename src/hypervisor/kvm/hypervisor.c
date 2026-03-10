/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <modvm/core/hypervisor.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>

#include "internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm: " fmt

/**
 * kvm_mem_map_cb - bridge between core memory allocator and KVM EPT/NPT.
 * @space: the abstract memory space.
 * @reg: the specific memory region to map.
 * @data: pointer to the KVM state structure.
 *
 * Invoked whenever the core adds a new memory region. Issues the 
 * KVM_SET_USER_MEMORY_REGION ioctl to bind HVA to GPA in hardware.
 *
 * return: 0 on success, or a negative error code.
 */
static int kvm_mem_map_cb(struct vm_mem_space *space, struct vm_mem_region *reg,
			  void *data)
{
	struct kvm_state *state = data;
	struct kvm_userspace_memory_region hw_reg = {
		.slot = state->mem_slot_idx++,
		.guest_phys_addr = reg->gpa,
		.memory_size = reg->size,
		.userspace_addr = (uint64_t)reg->hva,
		.flags = 0,
	};

	(void)space;

	if (reg->flags & VM_MEM_FLAG_READONLY)
		hw_reg.flags |= KVM_MEM_READONLY;

	if (ioctl(state->vm_fd, KVM_SET_USER_MEMORY_REGION, &hw_reg) < 0) {
		pr_err("failed to set hardware memory region: %d\n", errno);
		return -errno;
	}

	return 0;
}

/**
 * vm_hypervisor_create - initialize the KVM acceleration backend.
 * @hv: the core hypervisor context to populate.
 *
 * Opens /dev/kvm, validates API version, and creates the VM descriptor.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_hypervisor_create(struct vm_hypervisor *hv)
{
	struct kvm_state *state;
	int ret;

	if (WARN_ON(!hv))
		return -EINVAL;

	state = calloc(1, sizeof(*state));
	if (!state)
		return -ENOMEM;

	state->kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
	if (state->kvm_fd < 0) {
		pr_err("failed to open hypervisor device\n");
		ret = -errno;
		goto err_free_state;
	}

	ret = ioctl(state->kvm_fd, KVM_GET_API_VERSION, 0);
	if (ret != KVM_API_VERSION) {
		pr_err("unsupported hypervisor api version: %d\n", ret);
		ret = -ENOTSUP;
		goto err_close_kvm;
	}

	state->vm_fd = ioctl(state->kvm_fd, KVM_CREATE_VM, 0);
	if (state->vm_fd < 0) {
		pr_err("failed to instantiate virtual machine container\n");
		ret = -errno;
		goto err_close_kvm;
	}

	state->mem_slot_idx = 0;
	hv->priv = state;

	ret = vm_mem_space_init(&hv->mem_space, kvm_mem_map_cb, state);
	if (ret < 0) {
		pr_err("failed to initialize physical memory tracking\n");
		goto err_close_vm;
	}

	atomic_init(&hv->is_running, false);

	hv->init_mutex = os_mutex_create();
	if (!hv->init_mutex) {
		pr_err("failed to allocate startup synchronization lock\n");
		ret = -ENOMEM;
		vm_mem_space_destroy(&hv->mem_space);
		goto err_close_vm;
	}

	pr_info("hypervisor container established successfully\n");
	return 0;

err_close_vm:
	close(state->vm_fd);
err_close_kvm:
	close(state->kvm_fd);
err_free_state:
	free(state);
	return ret;
}

/**
 * vm_hypervisor_setup_irqchip - synthesize architectural interrupt chips.
 * @hv: the initialized hypervisor context.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_hypervisor_setup_irqchip(struct vm_hypervisor *hv)
{
	struct kvm_pit_config pit_conf = { .flags = 0 };
	struct kvm_state *state;

	if (WARN_ON(!hv || !hv->priv))
		return -EINVAL;

	state = hv->priv;

	if (ioctl(state->vm_fd, KVM_CREATE_IRQCHIP, 0) < 0) {
		pr_err("failed to synthesize architectural irqchip: %d\n",
		       errno);
		return -errno;
	}

	if (ioctl(state->vm_fd, KVM_CREATE_PIT2, &pit_conf) < 0) {
		pr_err("failed to synthesize programmable interval timer: %d\n",
		       errno);
		return -errno;
	}

	pr_info("architectural interrupt routing online\n");
	return 0;
}

/**
 * vm_hypervisor_set_irq - inject a hardware interrupt signal.
 * @hv: the hypervisor context.
 * @irq: the Global System Interrupt (GSI) number.
 * @level: logical voltage level (1 for high, 0 for low).
 *
 * return: 0 on success, or a negative error code.
 */
int vm_hypervisor_set_irq(struct vm_hypervisor *hv, uint32_t irq, int level)
{
	struct kvm_irq_level irq_level;
	struct kvm_state *state;

	if (WARN_ON(!hv || !hv->priv))
		return -EINVAL;

	state = hv->priv;

	irq_level.irq = irq;
	irq_level.level = level;

	if (ioctl(state->vm_fd, KVM_IRQ_LINE, &irq_level) < 0) {
		pr_err("failed to assert hardware irq line %u\n", irq);
		return -errno;
	}

	return 0;
}

/**
 * vm_hypervisor_destroy - release all host resources tied to KVM.
 * @hv: the hypervisor context to tear down.
 */
void vm_hypervisor_destroy(struct vm_hypervisor *hv)
{
	struct kvm_state *state;

	if (!hv || !hv->priv)
		return;

	state = hv->priv;

	if (hv->init_mutex) {
		os_mutex_destroy(hv->init_mutex);
		hv->init_mutex = NULL;
	}

	vm_mem_space_destroy(&hv->mem_space);
	close(state->vm_fd);
	close(state->kvm_fd);
	free(state);
	hv->priv = NULL;
}