/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_INTERNAL_THREAD_H
#define MODVM_INTERNAL_THREAD_H

#include <modvm/utils/stddef.h>

/*
 * OS Thread Internal APIs (Cross-Subsystem)
 *
 * These functions manipulate host thread signal states and are strictly
 * reserved for hardware acceleration backends (e.g., KVM vCPU execution loops).
 * Peripherals and board topologies MUST NOT use these.
 */

void os_thread_block_wakeup(void);
int os_thread_fill_wakeup_sigmask(void *buf, size_t size);

#endif /* MODVM_INTERNAL_THREAD_H */