/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/loader.h>
#include <modvm/core/ctxm.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>

#undef pr_fmt
#define pr_fmt(fmt) "loader: " fmt

#define MAX_LOADER_CLASSES 8

static const struct modvm_loader_class *loader_classes[MAX_LOADER_CLASSES];
static int nr_loader_classes = 0;

struct loader_instance_ctx {
	const struct modvm_loader_class *cls;
	void *priv;
};

static void loader_instance_release(void *data)
{
	struct loader_instance_ctx *inst = data;

	if (inst->cls->release && inst->priv)
		inst->cls->release(inst->priv);
}

/**
 * modvm_loader_class_register - statically register a boot protocol blueprint
 * @cls: the loader class definition to expose to the system
 */
void modvm_loader_class_register(const struct modvm_loader_class *cls)
{
	if (WARN_ON(!cls || !cls->name))
		return;

	if (WARN_ON(nr_loader_classes >= MAX_LOADER_CLASSES)) {
		pr_err("maximum loader registry capacity exceeded\n");
		return;
	}

	loader_classes[nr_loader_classes++] = cls;
}

static const struct modvm_loader_class *loader_class_find(const char *name)
{
	int i;

	if (WARN_ON(!name))
		return NULL;

	for (i = 0; i < nr_loader_classes; i++) {
		if (strcmp(loader_classes[i]->name, name) == 0)
			return loader_classes[i];
	}

	return NULL;
}

/**
 * modvm_loader_execute - discover, invoke, and persist a boot protocol
 * @ctx: the global machine context
 * @name: the string identifier of the requested protocol
 * @opts: arbitrary configuration string consumed by the loader backend
 *
 * This function decouples the motherboard topology from the software boot
 * process. It delegates memory injection and CPU state manipulation to the
 * selected loader, managing its lifecycle via ctxm.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_loader_execute(struct modvm_ctx *ctx, const char *name,
			 const char *opts)
{
	const struct modvm_loader_class *cls;
	struct loader_instance_ctx *inst;
	int ret;

	if (WARN_ON(!ctx || !name || !opts))
		return -EINVAL;

	cls = loader_class_find(name);
	if (!cls) {
		pr_err("boot protocol '%s' is not supported\n", name);
		return -ENOENT;
	}

	inst = modvm_ctxm_zalloc(ctx, sizeof(*inst));
	if (!inst)
		return -ENOMEM;

	inst->cls = cls;

	if (cls->load) {
		ret = cls->load(ctx, opts, &inst->priv);
		if (ret < 0) {
			pr_err("loader '%s' failed to inject payloads into memory\n",
			       name);
			return ret;
		}
	}

	/* Enqueue cleanup callback for graceful teardown */
	ret = __modvm_ctxm_add_action(ctx, loader_instance_release, inst);
	if (ret < 0) {
		loader_instance_release(inst);
		return ret;
	}

	if (cls->setup_bsp && ctx->vcpus[0]) {
		ret = cls->setup_bsp(ctx->vcpus[0], inst->priv);
		if (ret < 0) {
			pr_err("loader '%s' failed to manipulate processor state\n",
			       name);
			return ret;
		}
	}

	pr_info("successfully handed over execution to '%s' boot protocol\n",
		name);
	return 0;
}

/**
 * modvm_loader_load_raw - stream a binary payload directly into guest memory
 * @space: the target physical memory space
 * @path: host filesystem path to the payload
 * @gpa: destination physical address for the payload
 *
 * A generic utility function utilized by legacy raw loaders and testing
 * infrastructures to bypass complex protocol parsing.
 *
 * Return: 0 on success, or a negative error code.
 */
int modvm_loader_load_raw(struct modvm_mem_space *space, const char *path,
			  uint64_t gpa)
{
	FILE *fp;
	long size;
	size_t read_len;
	void *hva;

	if (WARN_ON(!space || !path))
		return -EINVAL;

	fp = fopen(path, "rb");
	if (!fp) {
		pr_err("failed to acquire image handle: %s (errno: %d)\n", path,
		       errno);
		return -ENOENT;
	}

	if (fseek(fp, 0, SEEK_END) < 0) {
		fclose(fp);
		return -EIO;
	}

	size = ftell(fp);
	if (size <= 0) {
		pr_err("payload image rejected due to zero or negative length: %s\n",
		       path);
		fclose(fp);
		return -EINVAL;
	}

	rewind(fp);

	hva = modvm_mem_gpa_to_hva(space, gpa);
	if (!hva) {
		pr_err("address translation trap: unmapped gpa 0x%lx\n", gpa);
		fclose(fp);
		return -EFAULT;
	}

	read_len = fread(hva, 1, (size_t)size, fp);
	if (read_len != (size_t)size) {
		pr_err("short stream read: expected %ld bytes, acquired %zu\n",
		       size, read_len);
		fclose(fp);
		return -EIO;
	}

	pr_info("successfully streamed %zu bytes from '%s' to gpa 0x%08lx\n",
		read_len, path, gpa);

	fclose(fp);
	return 0;
}