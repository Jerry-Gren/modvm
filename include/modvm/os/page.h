/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_OS_PAGE_H
#define MODVM_OS_PAGE_H

#include <stddef.h>

/**
 * os_page_get_size_bytes - retrieve the native page size of the host operating system
 *
 * Provides the hardware page granularity required for nested paging
 * structures like extended page tables.
 */
size_t os_page_get_size_bytes(void);

/**
 * os_page_allocate - allocate page-aligned memory from the host operating system
 * @size_bytes: the total size to allocate in bytes
 *
 * Guarantees that the returned memory pointer is page-aligned and
 * suitable for hypervisor mapping.
 */
void *os_page_allocate(size_t size_bytes);

/**
 * os_page_free - free memory previously allocated by the page allocator
 * @memory_pointer: pointer to the aligned memory block
 * @size_bytes: the size originally requested
 */
void os_page_free(void *memory_pointer, size_t size_bytes);

#endif /* MODVM_OS_PAGE_H */