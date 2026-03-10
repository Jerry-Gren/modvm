/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_OS_THREAD_H
#define MODVM_OS_THREAD_H

/**
 * typedef os_thread_func_t - Signature for host OS thread entry points.
 * @data: Pointer to caller-defined execution context.
 */
typedef void *(*os_thread_func_t)(void *data);

struct os_thread;
struct os_mutex;

struct os_thread *os_thread_create(os_thread_func_t func, void *data);
void os_thread_system_init(void);
int os_thread_join(struct os_thread *thread);
void os_thread_send_wakeup(struct os_thread *thread);
void os_thread_block_wakeup(void);
int os_thread_fill_wakeup_sigmask(void *buf, size_t size);
void os_thread_destroy(struct os_thread *thread);

struct os_mutex *os_mutex_create(void);
void os_mutex_lock(struct os_mutex *mutex);
void os_mutex_unlock(struct os_mutex *mutex);
void os_mutex_destroy(struct os_mutex *mutex);

#endif /* MODVM_OS_THREAD_H */