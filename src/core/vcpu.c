/* SPDX-License-Identifier: GPL-2.0 */
#include <errno.h>

#include <modvm/core/vcpu.h>
#include <modvm/utils/bug.h>

/**
 * vm_vcpu_init - allocate and map a virtual processor.
 * @vcpu: the vcpu structure to populate.
 * @hv: the parent hypervisor container.
 * @id: sequential index or architectural ID.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_vcpu_init(struct vm_vcpu *vcpu, struct vm_hypervisor *hv, int id)
{
	if (WARN_ON(!vcpu || !hv || !hv->cls))
		return -EINVAL;

	vcpu->id = id;
	vcpu->hv = hv;
	vcpu->ops = hv->cls->vcpu_ops;

	if (!vcpu->ops || !vcpu->ops->init)
		return -ENOTSUP;

	return vcpu->ops->init(vcpu);
}

/**
 * vm_vcpu_set_pc - configure the CPU reset vector.
 * @vcpu: the virtual processor.
 * @pc: the physical address to begin execution.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_vcpu_set_pc(struct vm_vcpu *vcpu, uint64_t pc)
{
	if (WARN_ON(!vcpu || !vcpu->ops))
		return -EINVAL;

	if (!vcpu->ops->set_pc)
		return -ENOTSUP;

	return vcpu->ops->set_pc(vcpu, pc);
}

/**
 * vm_vcpu_run - enter the execution loop of the processor.
 * @vcpu: the virtual processor to run.
 *
 * return: 0 on graceful exit, or a negative error code.
 */
int vm_vcpu_run(struct vm_vcpu *vcpu)
{
	if (WARN_ON(!vcpu || !vcpu->ops))
		return -EINVAL;

	if (!vcpu->ops->run)
		return -ENOTSUP;

	return vcpu->ops->run(vcpu);
}

/**
 * vm_vcpu_destroy - safely tear down virtual processor resources.
 * @vcpu: the virtual processor to destroy.
 */
void vm_vcpu_destroy(struct vm_vcpu *vcpu)
{
	if (!vcpu || !vcpu->ops)
		return;

	if (vcpu->ops->destroy)
		vcpu->ops->destroy(vcpu);
}