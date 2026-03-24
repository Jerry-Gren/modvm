/* SPDX-License-Identifier: GPL-2.0 */
#include <errno.h>

#include <modvm/core/vcpu.h>
#include <modvm/core/accel.h>
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
 * modvm_vcpu_get_regs - fetch architectural register state from backend
 * @vcpu: the virtual processor
 * @reg_class: the identifier of the register group to read
 * @buf: destination buffer
 * @size: expected size of the architectural register structure
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_vcpu_get_regs(struct modvm_vcpu *vcpu, enum modvm_reg_class reg_class,
			void *buf, size_t size)
{
	if (WARN_ON(!vcpu || !vcpu->ops || !buf || size == 0))
		return -EINVAL;

	if (!vcpu->ops->get_regs)
		return -ENOTSUP;

	return vcpu->ops->get_regs(vcpu, reg_class, buf, size);
}

/**
 * modvm_vcpu_set_regs - commit architectural register state to backend
 * @vcpu: the virtual processor
 * @reg_class: the identifier of the register group to write
 * @buf: source buffer containing the new state
 * @size: expected size of the architectural register structure
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_vcpu_set_regs(struct modvm_vcpu *vcpu, enum modvm_reg_class reg_class,
			const void *buf, size_t size)
{
	if (WARN_ON(!vcpu || !vcpu->ops || !buf || size == 0))
		return -EINVAL;

	if (!vcpu->ops->set_regs)
		return -ENOTSUP;

	return vcpu->ops->set_regs(vcpu, reg_class, buf, size);
}

/**
 * modvm_vcpu_get_reg - read a single abstract register
 * @vcpu: the virtual processor
 * @reg_id: abstract architectural register identifier
 * @val: pointer to store the retrieved value
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_vcpu_get_reg(struct modvm_vcpu *vcpu, uint64_t reg_id, uint64_t *val)
{
	if (WARN_ON(!vcpu || !vcpu->ops || !val))
		return -EINVAL;

	if (!vcpu->ops->get_reg)
		return -ENOTSUP;

	return vcpu->ops->get_reg(vcpu, reg_id, val);
}

/**
 * modvm_vcpu_set_reg - write a single abstract register
 * @vcpu: the virtual processor
 * @reg_id: abstract architectural register identifier
 * @val: the payload to commit
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_vcpu_set_reg(struct modvm_vcpu *vcpu, uint64_t reg_id, uint64_t val)
{
	if (WARN_ON(!vcpu || !vcpu->ops))
		return -EINVAL;

	if (!vcpu->ops->set_reg)
		return -ENOTSUP;

	return vcpu->ops->set_reg(vcpu, reg_id, val);
}

/**
 * modvm_vcpu_run - transition into the hardware execution loop
 * @vcpu: the virtual processor to run
 *
 * Return: 0 on successful guest exit, or a negative error code.
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