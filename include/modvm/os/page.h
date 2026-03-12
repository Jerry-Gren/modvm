/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_OS_PAGE_H
#define MODVM_OS_PAGE_H

#include <modvm/utils/stddef.h>

size_t os_page_size(void);
void *os_page_alloc(size_t size);
void os_page_free(void *ptr, size_t size);

#endif /* MODVM_OS_PAGE_H */