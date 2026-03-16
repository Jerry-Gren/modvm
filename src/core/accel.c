/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>
#include <errno.h>

#include <modvm/core/accel.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>

#include "internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "accel: " fmt

#define MAX_ACCEL_BACKENDS 8

static const struct modvm_accel_backend *accel_backends[MAX_ACCEL_BACKENDS];
static int nr_accel_backends = 0;

/**
 * modvm_accel_backend_register - statically register a hypervisor backend
 * @backend: the acceleration engine definition
 */
void modvm_accel_backend_register(const struct modvm_accel_backend *backend)
{
	if (WARN_ON(!backend || !backend->name))
		return;

	if (WARN_ON(nr_accel_backends >= MAX_ACCEL_BACKENDS)) {
		pr_err("maximum accelerator registry capacity exceeded\n");
		return;
	}

	accel_backends[nr_accel_backends++] = backend;
}

/**
 * modvm_accel_backend_find - retrieve an acceleration engine by its identifier
 * @name: the string identifier of the backend type
 *
 * Return: pointer to the backend definition, or NULL if unavailable.
 */
const struct modvm_accel_backend *modvm_accel_backend_find(const char *name)
{
	int i;

	if (WARN_ON(!name))
		return NULL;

	for (i = 0; i < nr_accel_backends; i++) {
		if (strcmp(accel_backends[i]->name, name) == 0)
			return accel_backends[i];
	}

	return NULL;
}

/**
 * modvm_accel_init - instantiate the virtual machine acceleration context
 * @accel: the hypervisor context state machine to populate
 * @name: the requested acceleration backend name
 * @bus: the system bus pointer to bind for MMIO/PIO dispatches
 *
 * Binds the abstract accelerator object to a specific platform driver
 * and triggers its initialization sequence.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_accel_init(struct modvm_accel *accel, const char *name,
		     struct modvm_bus *bus)
{
	if (WARN_ON(!accel || !name || !bus))
		return -EINVAL;

	accel->backend = modvm_accel_backend_find(name);
	if (!accel->backend) {
		pr_err("acceleration backend '%s' is not supported on this host\n",
		       name);
		return -ENOENT;
	}

	accel->bus = bus;

	if (WARN_ON(!accel->backend->ops || !accel->backend->ops->init))
		return -ENOTSUP;

	return accel->backend->ops->init(accel);
}

/**
 * modvm_accel_setup_irqchip - synthesize architectural interrupt routing
 * @accel: the initialized acceleration context
 *
 * Requests the underlying hypervisor to create an in-kernel interrupt
 * controller (like the local APIC or IOAPIC on x86) to maximize IRQ
 * routing performance.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_accel_setup_irqchip(struct modvm_accel *accel)
{
	if (WARN_ON(!accel || !accel->backend))
		return -EINVAL;

	if (!accel->backend->ops->setup_irqchip)
		return -ENOTSUP;

	return accel->backend->ops->setup_irqchip(accel);
}

/**
 * modvm_accel_set_irq - inject a hardware interrupt signal into the guest
 * @accel: the acceleration context
 * @gsi: the Global System Interrupt number to assert
 * @level: logical voltage level (1 for high, 0 for low)
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_accel_set_irq(struct modvm_accel *accel, uint32_t gsi, int level)
{
	if (WARN_ON(!accel || !accel->backend))
		return -EINVAL;

	if (!accel->backend->ops->set_irq)
		return -ENOTSUP;

	return accel->backend->ops->set_irq(accel, gsi, level);
}

/**
 * modvm_accel_map_ram - safely expose the physical memory allocator to the board
 * @accel: the active hypervisor context
 * @gpa: the guest physical address to map
 * @size: capacity of the memory bank in bytes
 * @flags: access permissions (e.g., MODVM_MEM_EXEC)
 *
 * Encapsulates the core memory space structure, preventing the hardware
 * board topology from directly manipulating internal acceleration state.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_accel_map_ram(struct modvm_accel *accel, uint64_t gpa, size_t size,
			uint32_t flags)
{
	if (WARN_ON(!accel))
		return -EINVAL;

	return modvm_mem_region_add(&accel->mem_space, gpa, size, flags);
}

/**
 * modvm_accel_destroy - tear down the accelerator and release host resources
 * @accel: the context to destroy
 */
void modvm_accel_destroy(struct modvm_accel *accel)
{
	if (WARN_ON(!accel || !accel->backend))
		return;

	if (accel->backend->ops->destroy)
		accel->backend->ops->destroy(accel);
}