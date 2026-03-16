/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_OS_THREAD_H
#define MODVM_OS_THREAD_H

#include <modvm/utils/stddef.h>
#include <modvm/utils/compiler.h>

/**
 * typedef os_thread_func_cb_t - signature for host OS thread entry points
 * @data: pointer to caller-defined execution closure
 */
typedef void *(*os_thread_func_cb_t)(void *data);

struct os_thread;
struct __ctx_lock_type(os_mutex) os_mutex;

struct os_thread *os_thread_create(os_thread_func_cb_t func, void *data);
void os_thread_system_init(void);
int os_thread_join(struct os_thread *thread);
void os_thread_send_wakeup(struct os_thread *thread);
void os_thread_destroy(struct os_thread *thread);

struct os_mutex *os_mutex_create(void);
void os_mutex_lock(struct os_mutex *mutex) __acquires(mutex);
void os_mutex_unlock(struct os_mutex *mutex) __releases(mutex);
void os_mutex_destroy(struct os_mutex *mutex);

#endif /* MODVM_OS_THREAD_H */