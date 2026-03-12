/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_LOADER_H
#define MODVM_CORE_LOADER_H

#include <modvm/core/modvm.h>
#include <modvm/core/memory.h>

/**
 * struct modvm_loader_class - blueprint for a pluggable boot protocol
 * @name: unique identifier (e.g., "linux-x86", "raw-bios")
 * @load: injects payloads into memory and establishes initial state.
 * Returns an opaque context pointer via out_priv.
 * @setup_bsp: manipulates the Bootstrap Processor (vCPU 0) to meet the
 * entry requirements of this specific protocol.
 * @release: frees any resources tied to the opaque context pointer.
 */
struct modvm_loader_class {
	const char *name;
	int (*load)(struct modvm_ctx *ctx, const char *opts, void **out_priv);
	int (*setup_bsp)(struct modvm_vcpu *vcpu, void *priv);
	void (*release)(void *priv);
};

void modvm_loader_class_register(const struct modvm_loader_class *cls);

int modvm_loader_execute(struct modvm_ctx *ctx, const char *name,
			 const char *opts);

int modvm_loader_load_raw(struct modvm_mem_space *space, const char *path,
			  uint64_t gpa);

#endif /* MODVM_CORE_LOADER_H */