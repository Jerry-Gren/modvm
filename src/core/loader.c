/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <errno.h>

#include <modvm/core/loader.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>

#undef pr_fmt
#define pr_fmt(fmt) "loader: " fmt

/**
 * vm_loader_load_raw - load a flat binary file into guest memory.
 * @space: the memory space to map the image into.
 * @path: host filesystem path to the binary image.
 * @gpa: guest physical address where the image should be loaded.
 *
 * return: 0 on success, or a negative error code.
 */
int vm_loader_load_raw(struct vm_mem_space *space, const char *path,
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
		pr_err("failed to open image file: %s (errno: %d)\n", path,
		       errno);
		return -ENOENT;
	}

	if (fseek(fp, 0, SEEK_END) < 0) {
		fclose(fp);
		return -EIO;
	}

	size = ftell(fp);
	if (size <= 0) {
		pr_err("invalid or empty image file: %s\n", path);
		fclose(fp);
		return -EINVAL;
	}

	rewind(fp);

	/*
	 * Translate the target guest physical address to the host virtual address.
	 * This resolves exactly where in our host process memory we need to
	 * write the file payload so the virtual processor can access it.
	 */
	hva = vm_mem_gpa_to_hva(space, gpa);
	if (IS_ERR_OR_NULL(hva)) {
		pr_err("failed to translate gpa 0x%lx for image loading\n",
		       gpa);
		fclose(fp);
		return -EFAULT;
	}

	/*
	 * Stream the file directly from the host filesystem into the
	 * virtual machine's physical memory backend.
	 */
	read_len = fread(hva, 1, (size_t)size, fp);
	if (read_len != (size_t)size) {
		pr_err("short read while loading image: expected %ld, got %zu\n",
		       size, read_len);
		fclose(fp);
		return -EIO;
	}

	pr_info("successfully loaded %zu bytes from '%s' to gpa 0x%08lx\n",
		read_len, path, gpa);

	fclose(fp);
	return 0;
}