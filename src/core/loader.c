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
 * vm_loader_load_raw_image - load a flat binary file into guest memory
 * @memory_space: the memory space to map the image into
 * @file_path: host filesystem path to the binary image
 * @guest_physical_address: guest physical address where the image should be loaded
 *
 * return: 0 on success, or a negative error code.
 */
int vm_loader_load_raw_image(struct vm_memory_space *memory_space,
			     const char *file_path,
			     uint64_t guest_physical_address)
{
	FILE *image_file;
	long total_file_size;
	size_t bytes_read;
	void *host_virtual_address;

	if (WARN_ON(!memory_space || !file_path))
		return -EINVAL;

	image_file = fopen(file_path, "rb");
	if (!image_file) {
		pr_err("failed to open image file: %s (errno: %d)\n", file_path,
		       errno);
		return -ENOENT;
	}

	if (fseek(image_file, 0, SEEK_END) < 0) {
		fclose(image_file);
		return -EIO;
	}

	total_file_size = ftell(image_file);
	if (total_file_size <= 0) {
		pr_err("invalid or empty image file: %s\n", file_path);
		fclose(image_file);
		return -EINVAL;
	}

	rewind(image_file);

	/*
	 * Translate the target guest physical address to the host virtual address.
	 * This resolves exactly where in our host process memory we need to
	 * write the file payload so the virtual processor can access it.
	 */
	host_virtual_address = vm_memory_guest_to_host_address(
		memory_space, guest_physical_address);
	if (IS_ERR_OR_NULL(host_virtual_address)) {
		pr_err("failed to translate address 0x%lx for image loading\n",
		       guest_physical_address);
		fclose(image_file);
		return -EFAULT;
	}

	/*
	 * Stream the file directly from the host filesystem into the
	 * virtual machine's physical memory backend.
	 */
	bytes_read = fread(host_virtual_address, 1, (size_t)total_file_size,
			   image_file);
	if (bytes_read != (size_t)total_file_size) {
		pr_err("short read while loading image: expected %ld, got %zu\n",
		       total_file_size, bytes_read);
		fclose(image_file);
		return -EIO;
	}

	pr_info("successfully loaded %zu bytes from '%s' to address 0x%08lx\n",
		bytes_read, file_path, guest_physical_address);

	fclose(image_file);
	return 0;
}