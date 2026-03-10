/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#include <modvm/os/page.h>
#include <modvm/utils/err.h>

/**
 * os_page_get_size_bytes - retrieve the native hardware page size
 *
 * return: the size of a single memory page in bytes.
 */
size_t os_page_get_size_bytes(void)
{
	return (size_t)sysconf(_SC_PAGESIZE);
}

/**
 * os_page_allocate - allocate page-aligned anonymous memory
 * @size_bytes: the total amount of memory to allocate
 *
 * Requests memory directly from the host kernel using mmap. The returned
 * pointer is guaranteed to be aligned to the boundary returned by
 * os_page_get_size_bytes(), which is strictly required for nested paging (EPT/NPT).
 *
 * return: pointer to the allocated memory, or an ERR_PTR on failure.
 */
void *os_page_allocate(size_t size_bytes)
{
	void *memory_pointer = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (memory_pointer == MAP_FAILED)
		return ERR_PTR(-ENOMEM);

	return memory_pointer;
}

/**
 * os_page_free - release previously allocated page-aligned memory
 * @memory_pointer: the base address of the memory block
 * @size_bytes: the exact size provided during allocation
 */
void os_page_free(void *memory_pointer, size_t size_bytes)
{
	if (memory_pointer && !IS_ERR(memory_pointer))
		munmap(memory_pointer, size_bytes);
}