/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <modvm/core/loader.h>
#include <modvm/core/memory.h>
#include <modvm/core/vcpu.h>
#include <modvm/arch/x86/regs.h>
#include <modvm/loader/e820.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>
#include <modvm/utils/cmdline.h>

#include <modvm/internal/loader.h>

#undef pr_fmt
#define pr_fmt(fmt) "linux_loader: " fmt

/* x86 Linux Boot Protocol standard offsets and magic numbers */
#define LINUX_MAGIC_HDR 0x53726448 /* "HdrS" */
#define BOOT_PARAM_E820_ENTRIES 0x01E8
#define BOOT_PARAM_HDR_OFFSET 0x01F1
#define BOOT_PARAM_E820_TABLE 0x02D0

#define SETUP_SECTS_OFFSET 0x01F1
#define SETUP_SYSSIZE_OFFSET 0x01F4
#define SETUP_VID_MODE 0x01FA
#define SETUP_MAGIC_OFFSET 0x0202
#define SETUP_TYPE_OF_LOADER 0x0210
#define SETUP_LOADFLAGS 0x0211
#define SETUP_RAMDISK_IMAGE 0x0218
#define SETUP_RAMDISK_SIZE 0x021C
#define SETUP_HEAP_END_PTR 0x0224
#define SETUP_CMDLINE_PTR 0x0228

/* Hardcoded memory placement addresses commonly used by VMMs */
#define ZERO_PAGE_GPA 0x090000ULL
#define CMDLINE_GPA 0x09E000ULL
#define KERNEL_GPA 0x100000ULL /* 1MB boundary for protected mode */
#define SAFE_BOOT_STACK 0x090000ULL /* Stack grows downwards from here */

/* Reused constants for architectural memory splitting */
#define PC_LOW_RAM_MAX 0xC0000000ULL
#define PC_HIGH_RAM_BASE 0x100000000ULL

struct linux_loader_ctx {
	uint64_t entry_pc;
	uint64_t zero_page;
};

/**
 * build_e820_table - synthesize the physical memory map for the guest kernel
 * @ctx: the global machine context containing ram configurations
 * @zero_page: host virtual address of the linux boot_params structure
 *
 * Injects E820 entries to describe standard usable RAM and the architectural
 * PCI memory hole, ensuring the guest OS maps hardware correctly.
 *
 * Return: 0 on success.
 */
static int build_e820_table(struct modvm_ctx *ctx, uint8_t *zero_page)
{
	struct modvm_e820_entry *table =
		(struct modvm_e820_entry *)(zero_page + BOOT_PARAM_E820_TABLE);
	uint8_t *nr_entries = zero_page + BOOT_PARAM_E820_ENTRIES;
	uint64_t ram_size = ctx->config.ram_size;
	int count = 0;

	table[count].addr = 0x00000000;
	table[count].size = 0x0009FC00;
	table[count].type = MODVM_E820_RAM;
	count++;

	table[count].addr = 0x0009FC00;
	table[count].size = 0x000A0000 - 0x0009FC00;
	table[count].type = MODVM_E820_RESERVED;
	count++;

	table[count].addr = 0x00100000;
	if (ram_size >= PC_LOW_RAM_MAX)
		table[count].size = PC_LOW_RAM_MAX - 0x00100000;
	else
		table[count].size = ram_size - 0x00100000;
	table[count].type = MODVM_E820_RAM;
	count++;

	if (ram_size > PC_LOW_RAM_MAX) {
		table[count].addr = PC_HIGH_RAM_BASE;
		table[count].size = ram_size - PC_LOW_RAM_MAX;
		table[count].type = MODVM_E820_RAM;
		count++;
	}

	*nr_entries = count;
	return 0;
}

/**
 * linux_loader_load - parse bzImage and inject it into guest physical memory
 * @ctx: the global machine context
 * @opts: configuration string provided by the user
 * @out_priv: output pointer for the opaque loader context
 *
 * Parses the Linux boot protocol header, loads the kernel payload,
 * optionally loads the initrd, and builds the zero page. Employs a single
 * unified error handling path to strictly prevent resource leakage.
 *
 * Return: 0 on success, or a negative error code.
 */
static int linux_loader_load(struct modvm_ctx *ctx, const char *opts,
			     void **out_priv)
{
	struct linux_loader_ctx *lctx;
	char *kernel_path = NULL;
	char *cmdline = NULL;
	char *initrd_path = NULL;
	FILE *fp = NULL;
	FILE *rd_fp = NULL;
	uint8_t *hva_zero_page;
	uint8_t *hva_kernel;
	uint8_t *hva_cmdline;
	uint8_t *hva_initrd;
	uint8_t header_buf[1024];
	uint32_t magic;
	uint8_t setup_sects;
	uint32_t setup_size;
	long file_size;
	long rd_size;
	size_t payload_size;
	uint64_t initrd_gpa;
	int ret = 0;

	kernel_path = cmdline_extract_opt(opts, "kernel");
	cmdline = cmdline_extract_opt(opts, "append");
	initrd_path = cmdline_extract_opt(opts, "initrd");

	if (WARN_ON(!kernel_path)) {
		pr_err("linux protocol strictly requires 'kernel=<path>' option\n");
		ret = -EINVAL;
		goto err_free_opts;
	}

	fp = fopen(kernel_path, "rb");
	if (!fp) {
		pr_err("failed to acquire bzImage handle: %s (errno: %d)\n",
		       kernel_path, errno);
		ret = -ENOENT;
		goto err_free_opts;
	}

	if (fread(header_buf, 1, sizeof(header_buf), fp) !=
	    sizeof(header_buf)) {
		pr_err("failed to read setup header from bzImage\n");
		ret = -EIO;
		goto err_close_kernel;
	}

	magic = *(uint32_t *)(header_buf + SETUP_MAGIC_OFFSET);
	if (magic != LINUX_MAGIC_HDR) {
		pr_err("invalid Linux magic signature: expected 0x%x, got 0x%x\n",
		       LINUX_MAGIC_HDR, magic);
		ret = -EINVAL;
		goto err_close_kernel;
	}

	setup_sects = *(uint8_t *)(header_buf + SETUP_SECTS_OFFSET);
	if (setup_sects == 0)
		setup_sects = 4;
	setup_size = (setup_sects + 1) * 512;

	fseek(fp, 0, SEEK_END);
	file_size = ftell(fp);
	if (file_size < setup_size) {
		pr_err("corrupted bzImage: file size smaller than setup data\n");
		ret = -EINVAL;
		goto err_close_kernel;
	}

	hva_zero_page =
		modvm_mem_gpa_to_hva(&ctx->accel.mem_space, ZERO_PAGE_GPA);
	hva_kernel = modvm_mem_gpa_to_hva(&ctx->accel.mem_space, KERNEL_GPA);
	hva_cmdline = modvm_mem_gpa_to_hva(&ctx->accel.mem_space, CMDLINE_GPA);

	if (!hva_zero_page || !hva_kernel || !hva_cmdline) {
		pr_err("failed to resolve guest physical memory for Linux injection\n");
		ret = -EFAULT;
		goto err_close_kernel;
	}

	memset(hva_zero_page, 0, 4096);
	memcpy(hva_zero_page + BOOT_PARAM_HDR_OFFSET,
	       header_buf + BOOT_PARAM_HDR_OFFSET,
	       sizeof(header_buf) - BOOT_PARAM_HDR_OFFSET);

	hva_zero_page[SETUP_TYPE_OF_LOADER] = 0xFF;

	if (cmdline) {
		strncpy((char *)hva_cmdline, cmdline, 4095);
		hva_cmdline[4095] = '\0';
		*(uint32_t *)(hva_zero_page + SETUP_CMDLINE_PTR) =
			(uint32_t)CMDLINE_GPA;
	} else {
		*(uint32_t *)(hva_zero_page + SETUP_CMDLINE_PTR) = 0;
	}

	build_e820_table(ctx, hva_zero_page);

	fseek(fp, setup_size, SEEK_SET);
	payload_size = file_size - setup_size;

	if (KERNEL_GPA + payload_size > PC_LOW_RAM_MAX) {
		pr_err("kernel payload exceeds low ram contiguous boundary\n");
		ret = -ENOSPC;
		goto err_close_kernel;
	}

	if (fread(hva_kernel, 1, payload_size, fp) != payload_size) {
		pr_err("short read while streaming kernel payload\n");
		ret = -EIO;
		goto err_close_kernel;
	}

	if (initrd_path) {
		rd_fp = fopen(initrd_path, "rb");
		if (!rd_fp) {
			pr_err("failed to acquire initrd handle: %s (errno: %d)\n",
			       initrd_path, errno);
			ret = -ENOENT;
			goto err_close_kernel;
		}

		fseek(rd_fp, 0, SEEK_END);
		rd_size = ftell(rd_fp);
		rewind(rd_fp);

		if (rd_size > 0) {
			initrd_gpa = KERNEL_GPA + payload_size;
			initrd_gpa = (initrd_gpa + 4095) & ~4095ULL;

			if (initrd_gpa + rd_size > PC_LOW_RAM_MAX) {
				pr_err("initrd payload exceeds low ram contiguous boundary\n");
				ret = -ENOSPC;
				goto err_close_initrd;
			}

			hva_initrd = modvm_mem_gpa_to_hva(&ctx->accel.mem_space,
							  initrd_gpa);
			if (!hva_initrd) {
				pr_err("failed to resolve guest memory for initrd\n");
				ret = -EFAULT;
				goto err_close_initrd;
			}

			if (fread(hva_initrd, 1, rd_size, rd_fp) !=
			    (size_t)rd_size) {
				pr_err("short read while streaming initrd payload\n");
				ret = -EIO;
				goto err_close_initrd;
			}

			*(uint32_t *)(hva_zero_page + SETUP_RAMDISK_IMAGE) =
				(uint32_t)initrd_gpa;
			*(uint32_t *)(hva_zero_page + SETUP_RAMDISK_SIZE) =
				(uint32_t)rd_size;

			pr_info("successfully streamed %ld bytes from '%s' to gpa 0x%08lx\n",
				rd_size, initrd_path, initrd_gpa);
		}

		fclose(rd_fp);
		rd_fp = NULL;
	}

	lctx = malloc(sizeof(*lctx));
	if (!lctx) {
		ret = -ENOMEM;
		goto err_close_kernel;
	}

	lctx->entry_pc = KERNEL_GPA;
	lctx->zero_page = ZERO_PAGE_GPA;
	*out_priv = lctx;

	pr_info("linux direct boot parameters successfully provisioned\n");
	pr_info("cmdline injected: %s\n", cmdline ? cmdline : "<none>");

	fclose(fp);
	free(kernel_path);
	free(cmdline);
	free(initrd_path);
	return 0;

err_close_initrd:
	if (rd_fp)
		fclose(rd_fp);
err_close_kernel:
	if (fp)
		fclose(fp);
err_free_opts:
	free(kernel_path);
	free(cmdline);
	free(initrd_path);
	return ret;
}

/**
 * linux_loader_setup_bsp - propel the processor into 32-bit protected mode
 * @vcpu: the bootstrap processor
 * @priv: the opaque linux context holding the entry point
 *
 * Employs architecture-agnostic register interfaces to bypass real mode
 * and establish a flat 4GB execution environment required by Linux.
 *
 * Return: 0 on success, or a negative error code.
 */
static int linux_loader_setup_bsp(struct modvm_vcpu *vcpu, void *priv)
{
	struct linux_loader_ctx *lctx = priv;
	struct modvm_x86_sregs sregs;
	struct modvm_x86_regs regs;
	int ret;

	ret = modvm_vcpu_get_regs(vcpu, MODVM_REG_SREGS, &sregs, sizeof(sregs));
	if (WARN_ON(ret < 0))
		return ret;

	sregs.cr0 = 0x01;
	sregs.cr4 = 0;

	sregs.cs.base = 0;
	sregs.cs.limit = 0xFFFFFFFF;
	sregs.cs.selector = 0x10;
	sregs.cs.type = 0x0b;
	sregs.cs.present = 1;
	sregs.cs.dpl = 0;
	sregs.cs.db = 1;
	sregs.cs.s = 1;
	sregs.cs.l = 0;
	sregs.cs.g = 1;
	sregs.cs.unusable = 0;

	sregs.ds.base = 0;
	sregs.ds.limit = 0xFFFFFFFF;
	sregs.ds.selector = 0x18;
	sregs.ds.type = 0x03;
	sregs.ds.present = 1;
	sregs.ds.dpl = 0;
	sregs.ds.db = 1;
	sregs.ds.s = 1;
	sregs.ds.l = 0;
	sregs.ds.g = 1;
	sregs.ds.unusable = 0;

	sregs.es = sregs.ds;
	sregs.fs = sregs.ds;
	sregs.gs = sregs.ds;
	sregs.ss = sregs.ds;

	ret = modvm_vcpu_set_regs(vcpu, MODVM_REG_SREGS, &sregs, sizeof(sregs));
	if (WARN_ON(ret < 0))
		return ret;

	memset(&regs, 0, sizeof(regs));
	regs.rflags = 0x02;
	regs.rip = lctx->entry_pc;
	regs.rsi = lctx->zero_page;

	ret = modvm_vcpu_set_regs(vcpu, MODVM_REG_GPR, &regs, sizeof(regs));
	if (WARN_ON(ret < 0))
		return ret;

	return 0;
}

/**
 * linux_loader_release - free memory associated with the loader context
 * @priv: the opaque linux context
 */
static void linux_loader_release(void *priv)
{
	free(priv);
}

static const struct modvm_loader_class linux_class = {
	.name = "linux-x86",
	.load = linux_loader_load,
	.setup_bsp = linux_loader_setup_bsp,
	.release = linux_loader_release,
};

static void __attribute__((constructor)) register_linux_loader(void)
{
	modvm_loader_class_register(&linux_class);
}