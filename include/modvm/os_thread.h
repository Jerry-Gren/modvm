/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_OS_THREAD_H
#define MODVM_OS_THREAD_H

/**
 * typedef os_thread_func_t - function signature for thread entry points
 * @opaque: pointer to caller-defined context data
 */
typedef void *(*os_thread_fn)(void *opaque);

struct os_thread;

/**
 * os_thread_create - spawn a new operating system thread
 * @func: the entry point function for the new thread
 * @opaque: context passed to the entry point
 *
 * Return: A valid thread handle on success, or an ERR_PTR on failure.
 */
struct os_thread *os_thread_create(os_thread_fn func, void *opaque);

/**
 * os_thread_system_init - initialize OS-specific thread environments
 *
 * Sets up necessary signal handlers or IPC mechanisms required for
 * thread management across platforms. Must be called once at boot.
 */
void os_thread_system_init(void);

/**
 * os_thread_join - block the caller until the specified thread terminates
 * @thread: the thread handle to wait upon
 *
 * Return: 0 on success, negative error code on failure.
 */
int os_thread_join(struct os_thread *thread);

/**
 * os_thread_kick - send a wake-up signal to a blocking thread
 * @thread: the thread handle to kick out of its blocking state
 */
void os_thread_kick(struct os_thread *thread);

/**
 * os_thread_mask_kick_signal - mask the asynchronous kick signal
 *
 * Blocks the delivery of the thread-kick signal in the current execution
 * context. This is crucial for preventing lost-wakeup race conditions
 * before transferring control to the hardware hypervisor backend.
 */
void os_thread_mask_kick_signal(void);

/**
 * os_thread_get_kick_signal - retrieve the dynamically allocated kick signal
 *
 * Hypervisor backends (e.g., KVM) must know which signal is used for thread
 * kicking in order to configure hardware-level signal masks properly during
 * context switches.
 *
 * Return: The POSIX signal number reserved for kicking threads.
 */
int os_thread_get_kick_signal(void);

/**
 * os_thread_destroy - release resources associated with a thread handle
 * @thread: the thread handle to destroy
 */
void os_thread_destroy(struct os_thread *thread);

/*
 * Opaque structure representing an OS-level mutual exclusion primitive.
 * The actual definition is private to the platform-specific implementation
 * (e.g., pthread_mutex_t on POSIX, CRITICAL_SECTION on Windows).
 */
struct os_mutex;

struct os_mutex *os_mutex_create(void);
void os_mutex_lock(struct os_mutex *mutex);
void os_mutex_unlock(struct os_mutex *mutex);
void os_mutex_destroy(struct os_mutex *mutex);

#endif /* MODVM_OS_THREAD_H */