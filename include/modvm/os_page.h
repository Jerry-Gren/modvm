/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_OS_PAGE_H
#define MODVM_OS_PAGE_H

#include <stddef.h>

/**
 * os_get_page_size - retrieve the native page size of the host operating system
 *
 * Return: The page size in bytes (e.g., 4096 on typical x86_64, 16384 on Apple Silicon)
 */
size_t os_get_page_size(void);

/**
 * os_alloc_pages - allocate page-aligned memory from the host OS
 * @size: the total size to allocate (must be a multiple of page size)
 *
 * This memory must be suitable for hypervisor nested paging (EPT/NPT).
 *
 * Return: Pointer to the allocated memory, or NULL on failure.
 */
void *os_alloc_pages(size_t size);

/**
 * os_free_pages - free memory previously allocated by os_alloc_pages
 * @ptr: pointer to the memory
 * @size: the size originally requested
 */
void os_free_pages(void *ptr, size_t size);

#endif /* MODVM_OS_PAGE_H */