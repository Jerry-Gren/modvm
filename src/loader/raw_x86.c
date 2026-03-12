/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/loader.h>
#include <modvm/core/vcpu.h>
#include <modvm/arch/x86/regs.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/log.h>

#undef pr_fmt
#define pr_fmt(fmt) "raw_loader: " fmt

static int raw_loader_load(struct modvm_ctx *ctx, const char *opts,
			   void **out_priv)
{
	/* Treat opts directly as the file path for simplicity */
	if (WARN_ON(!opts || strlen(opts) == 0))
		return -EINVAL;

	*out_priv = NULL; /* No state needed */

	return modvm_loader_load_raw(&ctx->accel.mem_space, opts, 0x0000);
}

static int raw_loader_setup_bsp(struct modvm_vcpu *vcpu, void *priv)
{
	struct modvm_x86_sregs sregs;
	struct modvm_x86_regs regs;
	int ret;

	(void)priv;

	ret = modvm_vcpu_get_regs(vcpu, MODVM_REG_CLASS_X86_SREGS, &sregs,
				  sizeof(sregs));
	if (WARN_ON(ret < 0))
		return ret;

	/* Legacy PC Real Mode initialization vector */
	sregs.cs.selector = 0xF000;
	sregs.cs.base = 0xFFFF0000;

	ret = modvm_vcpu_set_regs(vcpu, MODVM_REG_CLASS_X86_SREGS, &sregs,
				  sizeof(sregs));
	if (WARN_ON(ret < 0))
		return ret;

	memset(&regs, 0, sizeof(regs));
	regs.rip = 0xFFF0;
	regs.rflags = 0x02;

	return modvm_vcpu_set_regs(vcpu, MODVM_REG_CLASS_X86_GPR, &regs,
				   sizeof(regs));
}

static const struct modvm_loader_class raw_class = {
	.name = "raw-x86",
	.load = raw_loader_load,
	.setup_bsp = raw_loader_setup_bsp,
};

static void __attribute__((constructor)) register_raw_loader(void)
{
	modvm_loader_class_register(&raw_class);
}