/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#include <modvm/os/page.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

/**
 * os_page_size - retrieve the native hardware page size
 *
 * Interrogates the host operating system for the underlying architecture's
 * default page frame size (typically 4KB).
 *
 * Return: the size of a single memory page in bytes.
 */
size_t os_page_size(void)
{
	return (size_t)sysconf(_SC_PAGESIZE);
}

/**
 * os_page_alloc - allocate page-aligned anonymous memory
 * @size: the total amount of memory to allocate in bytes
 *
 * Requests memory directly from the host kernel via mmap. The returned
 * pointer is guaranteed to be strictly page-aligned, which is an absolute
 * requirement for nested paging mechanisms like Intel EPT or AMD NPT.
 *
 * Return: pointer to the allocated memory, or an ERR_PTR on failure.
 */
void *os_page_alloc(size_t size)
{
	void *ptr;

	if (WARN_ON(size == 0))
		return ERR_PTR(-EINVAL);

	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (unlikely(ptr == MAP_FAILED))
		return ERR_PTR(-ENOMEM);

#ifdef MADV_HUGEPAGE
	/* Advice the kernel to use huge page */
	if (madvise(ptr, size, MADV_HUGEPAGE) < 0) {
		pr_debug(
			"host OS rejected hugepage advice (errno: %d), falling back to 4KB pages\n",
			errno);
	}
#endif
	return ptr;
}

/**
 * os_page_free - release previously allocated page-aligned memory
 * @ptr: the base address of the memory block
 * @size: the exact size provided during allocation
 */
void os_page_free(void *ptr, size_t size)
{
	if (WARN_ON(!ptr || IS_ERR(ptr) || size == 0))
		return;

	munmap(ptr, size);
}