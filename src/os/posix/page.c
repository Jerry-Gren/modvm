/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#include <modvm/os/page.h>
#include <modvm/utils/err.h>

/**
 * os_page_size - Retrieve the native hardware page size.
 *
 * Return: The size of a single memory page in bytes.
 */
size_t os_page_size(void)
{
	return (size_t)sysconf(_SC_PAGESIZE);
}

/**
 * os_page_alloc - Allocate page-aligned anonymous memory.
 * @size: The total amount of memory to allocate in bytes.
 *
 * Requests memory directly from the host kernel using mmap. The returned
 * pointer is guaranteed to be page-aligned, which is strictly required
 * for nested paging (EPT/NPT).
 *
 * Return: Pointer to the allocated memory, or an ERR_PTR on failure.
 */
void *os_page_alloc(size_t size)
{
	void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (ptr == MAP_FAILED)
		return ERR_PTR(-ENOMEM);

	return ptr;
}

/**
 * os_page_free - Release previously allocated page-aligned memory.
 * @ptr: The base address of the memory block.
 * @size: The exact size provided during allocation.
 */
void os_page_free(void *ptr, size_t size)
{
	if (ptr && !IS_ERR(ptr))
		munmap(ptr, size);
}