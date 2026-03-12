/* SPDX-License-Identifier: GPL-2.0 */
#include <errno.h>

#include <modvm/core/vcpu.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

/**
 * modvm_vcpu_init - allocate and map a virtual processor
 * @vcpu: the vcpu structure to populate
 * @accel: the parent acceleration container
 * @id: sequential architectural ID
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_vcpu_init(struct modvm_vcpu *vcpu, struct modvm_accel *accel, int id)
{
	if (WARN_ON(!vcpu || !accel || !accel->backend))
		return -EINVAL;

	vcpu->id = id;
	vcpu->accel = accel;
	vcpu->ops = accel->backend->vcpu_ops;

	if (WARN_ON(!vcpu->ops || !vcpu->ops->init))
		return -ENOTSUP;

	return vcpu->ops->init(vcpu);
}

/**
 * modvm_vcpu_set_pc - configure the CPU instruction pointer
 * @vcpu: the virtual processor
 * @pc: the physical address to begin execution
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_vcpu_set_pc(struct modvm_vcpu *vcpu, uint64_t pc)
{
	if (WARN_ON(!vcpu || !vcpu->ops))
		return -EINVAL;

	if (!vcpu->ops->set_pc)
		return -ENOTSUP;

	return vcpu->ops->set_pc(vcpu, pc);
}

/**
 * modvm_vcpu_run - transition into the hardware execution loop
 * @vcpu: the virtual processor to run
 *
 * Return: 0 on graceful guest exit, or a negative error code.
 */
int modvm_vcpu_run(struct modvm_vcpu *vcpu)
{
	if (WARN_ON(!vcpu || !vcpu->ops))
		return -EINVAL;

	if (!vcpu->ops->run)
		return -ENOTSUP;

	return vcpu->ops->run(vcpu);
}

/**
 * modvm_vcpu_destroy - safely release hardware processor resources
 * @vcpu: the virtual processor to destroy
 */
void modvm_vcpu_destroy(struct modvm_vcpu *vcpu)
{
	if (WARN_ON(!vcpu || !vcpu->ops))
		return;

	if (vcpu->ops->destroy)
		vcpu->ops->destroy(vcpu);
}