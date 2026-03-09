/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <errno.h>

#include <modvm/loader.h>
#include <modvm/log.h>
#include <modvm/err.h>
#include <modvm/bug.h>

#undef pr_fmt
#define pr_fmt(fmt) "loader: " fmt

int loader_load_raw_image(struct vm_memory_space *space, const char *path,
			  uint64_t gpa)
{
	FILE *file;
	long file_size;
	size_t read_bytes;
	void *hva;

	if (WARN_ON(!space || !path))
		return -EINVAL;

	/* Open the file in binary mode to prevent text translation */
	file = fopen(path, "rb");
	if (!file) {
		pr_err("Failed to open image file: %s (errno: %d)\n", path,
		       errno);
		return -ENOENT;
	}

	/* Probe the size of the binary file */
	if (fseek(file, 0, SEEK_END) < 0) {
		fclose(file);
		return -EIO;
	}

	file_size = ftell(file);
	if (file_size <= 0) {
		pr_err("Invalid or empty image file: %s\n", path);
		fclose(file);
		return -EINVAL;
	}

	/* Rewind file pointer back to the beginning for reading */
	rewind(file);

	/*
	 * Translate the target Guest Physical Address to the Host Virtual Address.
	 * This tells us exactly where in our host process memory we need to
	 * write the file payload so the vCPU can see it.
	 */
	hva = vm_memory_gpa_to_hva(space, gpa);
	if (IS_ERR_OR_NULL(hva)) {
		pr_err("Failed to translate GPA 0x%lx for image loading\n",
		       gpa);
		fclose(file);
		return -EFAULT;
	}

	/*
	 * Stream the file directly from the host filesystem into the
	 * virtual machine's physical memory backend.
	 */
	read_bytes = fread(hva, 1, (size_t)file_size, file);
	if (read_bytes != (size_t)file_size) {
		pr_err("Short read while loading image: expected %ld, got %zu\n",
		       file_size, read_bytes);
		fclose(file);
		return -EIO;
	}

	pr_info("Successfully loaded %zu bytes from '%s' to GPA 0x%08lx\n",
		read_bytes, path, gpa);

	fclose(file);
	return 0;
}