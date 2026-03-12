/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <errno.h>

#include <modvm/core/loader.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "loader: " fmt

/**
 * modvm_loader_load_raw - stream a binary payload into guest memory
 * @space: the target physical memory space
 * @path: host filesystem path to the payload
 * @gpa: destination physical address for the payload
 *
 * Resolves the physical address into the host's virtual address space
 * and performs a direct stream read, bypassing typical bootloaders.
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
	if (unlikely(!fp)) {
		pr_err("failed to acquire image handle: %s (errno: %d)\n", path,
		       errno);
		return -ENOENT;
	}

	if (unlikely(fseek(fp, 0, SEEK_END) < 0)) {
		fclose(fp);
		return -EIO;
	}

	size = ftell(fp);
	if (unlikely(size <= 0)) {
		pr_err("payload image rejected due to zero or negative length: %s\n",
		       path);
		fclose(fp);
		return -EINVAL;
	}

	rewind(fp);

	hva = modvm_mem_gpa_to_hva(space, gpa);
	if (unlikely(IS_ERR_OR_NULL(hva))) {
		pr_err("address translation trap: unmapped gpa 0x%lx\n", gpa);
		fclose(fp);
		return -EFAULT;
	}

	read_len = fread(hva, 1, (size_t)size, fp);
	if (unlikely(read_len != (size_t)size)) {
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