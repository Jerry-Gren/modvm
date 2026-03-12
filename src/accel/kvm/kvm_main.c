/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdatomic.h>

#include <modvm/core/accel.h>
#include <modvm/core/vcpu.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>

#include "internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "kvm: " fmt

/**
 * kvm_mem_map_cb - bridge between core memory allocator and KVM hardware paging
 * @space: the abstract memory space
 * @reg: the specific memory region to map
 * @data: pointer to the private KVM state structure
 *
 * Invoked dynamically whenever the core allocates a new memory region.
 * It strictly binds the host virtual address to the guest physical address
 * via the KVM_SET_USER_MEMORY_REGION ioctl.
 *
 * Return: 0 on success, or a negative error code.
 */
static int kvm_mem_map_cb(struct modvm_mem_space *space,
			  struct modvm_mem_region *reg, void *data)
{
	struct modvm_kvm_state *state = data;
	struct kvm_userspace_memory_region hw_reg = {
		.slot = state->mem_slot_idx++,
		.guest_phys_addr = reg->gpa,
		.memory_size = reg->size,
		.userspace_addr = (uint64_t)reg->hva,
		.flags = 0,
	};

	(void)space;

	if (reg->flags & MODVM_MEM_READONLY)
		hw_reg.flags |= KVM_MEM_READONLY;

	if (ioctl(state->vm_fd, KVM_SET_USER_MEMORY_REGION, &hw_reg) < 0) {
		pr_err("failed to commit hardware memory slot: %d\n", errno);
		return -errno;
	}

	return 0;
}

/**
 * kvm_accel_init - initialize the KVM acceleration context
 * @accel: the acceleration engine object to populate
 *
 * Opens the hypervisor device node, validates the API version, and creates
 * the root virtual machine file descriptor.
 *
 * Return: 0 on success, or a negative error code.
 */
static int kvm_accel_init(struct modvm_accel *accel)
{
	struct modvm_kvm_state *state;
	int ret;

	state = calloc(1, sizeof(*state));
	if (!state)
		return -ENOMEM;

	state->kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
	if (state->kvm_fd < 0) {
		pr_err("failed to open hypervisor device node\n");
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
	accel->priv = state;

	ret = modvm_mem_space_init(&accel->mem_space, kvm_mem_map_cb, state);
	if (ret < 0) {
		pr_err("failed to initialize physical memory tracking\n");
		goto err_close_vm;
	}

	atomic_init(&accel->is_running, false);

	accel->init_mutex = os_mutex_create();
	if (IS_ERR(accel->init_mutex)) {
		pr_err("failed to allocate startup synchronization lock\n");
		ret = PTR_ERR(accel->init_mutex);
		accel->init_mutex = NULL;
		modvm_mem_space_destroy(&accel->mem_space);
		goto err_close_vm;
	}

	pr_info("acceleration container established successfully\n");
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
 * kvm_accel_setup_irqchip - synthesize architectural interrupt controllers
 * @accel: the initialized acceleration context
 *
 * Requests the kernel to instantiate the local APIC, IOAPIC, and legacy PIT.
 *
 * Return: 0 on success, or a negative error code.
 */
static int kvm_accel_setup_irqchip(struct modvm_accel *accel)
{
	struct kvm_pit_config pit_conf = { .flags = 0 };
	struct modvm_kvm_state *state = accel->priv;

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
 * kvm_accel_set_irq - inject a hardware interrupt signal
 * @accel: the acceleration context
 * @gsi: the Global System Interrupt number
 * @level: logical voltage level (1 for high, 0 for low)
 *
 * Return: 0 on success, or a negative error code.
 */
static int kvm_accel_set_irq(struct modvm_accel *accel, uint32_t gsi, int level)
{
	struct kvm_irq_level irq_level;
	struct modvm_kvm_state *state = accel->priv;

	irq_level.irq = gsi;
	irq_level.level = level;

	if (ioctl(state->vm_fd, KVM_IRQ_LINE, &irq_level) < 0) {
		pr_err("failed to assert hardware irq line %u\n", gsi);
		return -errno;
	}

	return 0;
}

/**
 * kvm_accel_destroy - release host resources tied to the KVM subsystem
 * @accel: the acceleration context to tear down
 */
static void kvm_accel_destroy(struct modvm_accel *accel)
{
	struct modvm_kvm_state *state = accel->priv;

	if (accel->init_mutex) {
		os_mutex_destroy(accel->init_mutex);
		accel->init_mutex = NULL;
	}

	modvm_mem_space_destroy(&accel->mem_space);
	close(state->vm_fd);
	close(state->kvm_fd);
	free(state);
	accel->priv = NULL;
}

static const struct modvm_accel_ops kvm_ops = {
	.init = kvm_accel_init,
	.destroy = kvm_accel_destroy,
	.setup_irqchip = kvm_accel_setup_irqchip,
	.set_irq = kvm_accel_set_irq,
};

static const struct modvm_accel_backend kvm_backend = {
	.name = "kvm",
	.ops = &kvm_ops,
	.vcpu_ops = &modvm_kvm_vcpu_ops,
};

static void __attribute__((constructor)) register_kvm_backend(void)
{
	modvm_accel_backend_register(&kvm_backend);
}