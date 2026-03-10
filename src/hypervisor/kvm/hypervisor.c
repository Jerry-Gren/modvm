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
#define pr_fmt(fmt) "kvm_hypervisor: " fmt

/**
 * kvm_memory_map_callback - bridge between core memory allocator and KVM EPT
 * @space: the abstract memory space
 * @region: the specific memory region to map
 * @context_data: pointer to the KVM state structure
 *
 * This callback is invoked whenever the core adds a new memory region.
 * It issues the KVM_SET_USER_MEMORY_REGION ioctl to bind the host virtual
 * address to the guest physical address in the hardware page tables.
 *
 * return: 0 on success, or a negative error code on ioctl failure.
 */
static int kvm_memory_map_callback(struct vm_memory_space *space,
				   struct vm_memory_region *region,
				   void *context_data)
{
	struct vm_kvm_state *kvm_state = context_data;
	struct kvm_userspace_memory_region hardware_region = {
		.slot = kvm_state->memory_slot_index++,
		.guest_phys_addr = region->guest_physical_address,
		.memory_size = region->size_bytes,
		.userspace_addr = (uint64_t)region->host_virtual_address,
		.flags = 0,
	};

	(void)space;

	if (region->access_flags & VM_MEMORY_FLAG_READONLY)
		hardware_region.flags |= KVM_MEM_READONLY;

	if (ioctl(kvm_state->virtual_machine_file_descriptor,
		  KVM_SET_USER_MEMORY_REGION, &hardware_region) < 0) {
		pr_err("failed to set hardware memory region: %d\n", errno);
		return -errno;
	}

	return 0;
}

/**
 * vm_hypervisor_create - initialize the KVM acceleration backend
 * @hypervisor: the core hypervisor context to populate
 *
 * Opens the /dev/kvm character device, validates the API version, and
 * creates the virtual machine container descriptor.
 *
 * return: 0 on success, or a negative error code on failure.
 */
int vm_hypervisor_create(struct vm_hypervisor *hypervisor)
{
	struct vm_kvm_state *kvm_state;
	int return_code;

	if (WARN_ON(!hypervisor))
		return -EINVAL;

	kvm_state = calloc(1, sizeof(*kvm_state));
	if (!kvm_state)
		return -ENOMEM;

	kvm_state->kvm_file_descriptor = open("/dev/kvm", O_RDWR | O_CLOEXEC);
	if (kvm_state->kvm_file_descriptor < 0) {
		pr_err("failed to open hypervisor device\n");
		return_code = -errno;
		goto error_free_kvm_state;
	}

	return_code =
		ioctl(kvm_state->kvm_file_descriptor, KVM_GET_API_VERSION, 0);
	if (return_code != KVM_API_VERSION) {
		pr_err("unsupported hypervisor api version: %d\n", return_code);
		return_code = -ENOTSUP;
		goto error_close_kvm_descriptor;
	}

	kvm_state->virtual_machine_file_descriptor =
		ioctl(kvm_state->kvm_file_descriptor, KVM_CREATE_VM, 0);
	if (kvm_state->virtual_machine_file_descriptor < 0) {
		pr_err("failed to instantiate virtual machine container\n");
		return_code = -errno;
		goto error_close_kvm_descriptor;
	}

	kvm_state->memory_slot_index = 0;
	hypervisor->hypervisor_private_data = kvm_state;

	return_code = vm_memory_space_init(&hypervisor->memory_space,
					   kvm_memory_map_callback, kvm_state);
	if (return_code < 0) {
		pr_err("failed to initialize physical memory tracking\n");
		goto error_close_virtual_machine_descriptor;
	}

	atomic_init(&hypervisor->is_running, false);

	hypervisor->startup_synchronization_lock = os_mutex_create();
	if (!hypervisor->startup_synchronization_lock) {
		pr_err("failed to allocate startup synchronization lock\n");
		return_code = -ENOMEM;
		vm_memory_space_destroy(&hypervisor->memory_space);
		goto error_close_virtual_machine_descriptor;
	}

	pr_info("hypervisor container established successfully\n");
	return 0;

error_close_virtual_machine_descriptor:
	close(kvm_state->virtual_machine_file_descriptor);
error_close_kvm_descriptor:
	close(kvm_state->kvm_file_descriptor);
error_free_kvm_state:
	free(kvm_state);
	return return_code;
}

/**
 * vm_hypervisor_setup_interrupt_controller - synthesize architectural interrupt chips
 * @hypervisor: the initialized hypervisor context
 *
 * Instantiates the virtual Programmable Interrupt Controller (PIC),
 * IOAPIC, and local APICs required for legacy x86 interrupt routing.
 */
int vm_hypervisor_setup_interrupt_controller(struct vm_hypervisor *hypervisor)
{
	struct kvm_pit_config pit_config = { .flags = 0 };
	struct vm_kvm_state *kvm_state;

	if (WARN_ON(!hypervisor || !hypervisor->hypervisor_private_data))
		return -EINVAL;

	kvm_state = hypervisor->hypervisor_private_data;

	if (ioctl(kvm_state->virtual_machine_file_descriptor,
		  KVM_CREATE_IRQCHIP, 0) < 0) {
		pr_err("failed to synthesize architectural interrupt controller: %d\n",
		       errno);
		return -errno;
	}

	if (ioctl(kvm_state->virtual_machine_file_descriptor, KVM_CREATE_PIT2,
		  &pit_config) < 0) {
		pr_err("failed to synthesize programmable interval timer: %d\n",
		       errno);
		return -errno;
	}

	pr_info("architectural interrupt routing online\n");
	return 0;
}

/**
 * vm_hypervisor_set_interrupt_line - inject a hardware interrupt signal
 * @hypervisor: the hypervisor context
 * @line_number: the Global System Interrupt (GSI) number
 * @level: logical voltage level (1 for high, 0 for low)
 */
int vm_hypervisor_set_interrupt_line(struct vm_hypervisor *hypervisor,
				     uint32_t line_number, int level)
{
	struct kvm_irq_level irq_level;
	struct vm_kvm_state *kvm_state;

	if (WARN_ON(!hypervisor || !hypervisor->hypervisor_private_data))
		return -EINVAL;

	kvm_state = hypervisor->hypervisor_private_data;

	irq_level.irq = line_number;
	irq_level.level = level;

	if (ioctl(kvm_state->virtual_machine_file_descriptor, KVM_IRQ_LINE,
		  &irq_level) < 0) {
		pr_err("failed to assert hardware interrupt line %u\n",
		       line_number);
		return -errno;
	}

	return 0;
}

/**
 * vm_hypervisor_destroy - release all host resources tied to the hypervisor
 * @hypervisor: the hypervisor context to tear down
 */
void vm_hypervisor_destroy(struct vm_hypervisor *hypervisor)
{
	struct vm_kvm_state *kvm_state;

	if (!hypervisor || !hypervisor->hypervisor_private_data)
		return;

	kvm_state = hypervisor->hypervisor_private_data;

	if (hypervisor->startup_synchronization_lock) {
		os_mutex_destroy(hypervisor->startup_synchronization_lock);
		hypervisor->startup_synchronization_lock = NULL;
	}

	vm_memory_space_destroy(&hypervisor->memory_space);
	close(kvm_state->virtual_machine_file_descriptor);
	close(kvm_state->kvm_file_descriptor);
	free(kvm_state);
	hypervisor->hypervisor_private_data = NULL;
}