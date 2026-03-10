/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>
#include <errno.h>

#include <modvm/core/hypervisor.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>

#undef pr_fmt
#define pr_fmt(fmt) "hypervisor: " fmt

#define MAX_HV_CLASSES 8

static const struct vm_hv_class *hv_classes[MAX_HV_CLASSES];
static int nr_hv_classes = 0;

/**
 * vm_hv_class_register - register a new hypervisor backend blueprint.
 * @cls: the hypervisor class to register.
 */
void vm_hv_class_register(const struct vm_hv_class *cls)
{
	if (nr_hv_classes < MAX_HV_CLASSES)
		hv_classes[nr_hv_classes++] = cls;
}

/**
 * vm_hv_class_find - retrieve a hypervisor blueprint by name.
 * @name: the string identifier of the backend type.
 *
 * return: pointer to the hypervisor class, or NULL if not found.
 */
const struct vm_hv_class *vm_hv_class_find(const char *name)
{
	int i;

	for (i = 0; i < nr_hv_classes; i++) {
		if (strcmp(hv_classes[i]->name, name) == 0)
			return hv_classes[i];
	}

	return NULL;
}

/**
 * vm_hypervisor_init - initialize the virtual machine acceleration context.
 * @hv: the hypervisor context to populate.
 * @accel_name: the name of the requested acceleration backend.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_hypervisor_init(struct vm_hypervisor *hv, const char *accel_name)
{
	if (WARN_ON(!hv || !accel_name))
		return -EINVAL;

	hv->cls = vm_hv_class_find(accel_name);
	if (!hv->cls) {
		pr_err("hypervisor backend '%s' is not registered\n",
		       accel_name);
		return -ENOENT;
	}

	if (!hv->cls->ops || !hv->cls->ops->init)
		return -ENOTSUP;

	return hv->cls->ops->init(hv);
}

/**
 * vm_hypervisor_setup_irqchip - synthesize architectural interrupt chips.
 * @hv: the initialized hypervisor context.
 *
 * Return: 0 on success, or a negative error code.
 */
int vm_hypervisor_setup_irqchip(struct vm_hypervisor *hv)
{
	if (WARN_ON(!hv || !hv->cls))
		return -EINVAL;

	if (!hv->cls->ops->setup_irqchip)
		return -ENOTSUP;

	return hv->cls->ops->setup_irqchip(hv);
}

/**
 * vm_hypervisor_set_irq - inject a hardware interrupt signal.
 * @hv: the hypervisor context.
 * @gsi: the Global System Interrupt number.
 * @level: logical voltage level (1 for high, 0 for low).
 *
 * return: 0 on success, or a negative error code.
 */
int vm_hypervisor_set_irq(struct vm_hypervisor *hv, uint32_t gsi, int level)
{
	if (WARN_ON(!hv || !hv->cls))
		return -EINVAL;

	if (!hv->cls->ops->set_irq)
		return -ENOTSUP;

	return hv->cls->ops->set_irq(hv, gsi, level);
}

/**
 * vm_hypervisor_destroy - release all host resources tied to the accelerator.
 * @hv: the hypervisor context to tear down.
 */
void vm_hypervisor_destroy(struct vm_hypervisor *hv)
{
	if (!hv || !hv->cls)
		return;

	if (hv->cls->ops->destroy)
		hv->cls->ops->destroy(hv);
}