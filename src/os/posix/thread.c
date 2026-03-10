/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include <modvm/os/thread.h>
#include <modvm/utils/err.h>

struct os_thread {
	pthread_t handle;
};

struct os_mutex {
	pthread_mutex_t handle;
};

static int wakeup_signum = -1;

static void empty_sig_handler(int signum)
{
	(void)signum;
}

/**
 * os_thread_system_init - Initialize the global threading subsystem.
 *
 * Scans the POSIX real-time signal range to allocate an exclusive, unused
 * signal number for waking up blocking hypervisor threads.
 */
void os_thread_system_init(void)
{
	struct sigaction sa;
	struct sigaction old_sa;
	int signum;

	if (wakeup_signum != -1)
		return;

	sa.sa_handler = empty_sig_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	/*
	 * scan the POSIX real-time signal range for an unused slot.
	 * This prevents hostile takeover of standard signals like SIGUSR1,
	 * ensuring the hypervisor engine remains a polite resident.
	 */
	for (signum = SIGRTMIN; signum <= SIGRTMAX; signum++) {
		if (sigaction(signum, NULL, &old_sa) == 0) {
			if (old_sa.sa_handler == SIG_DFL ||
			    old_sa.sa_handler == SIG_IGN) {
				if (sigaction(signum, &sa, NULL) == 0) {
					wakeup_signum = signum;
					break;
				}
			}
		}
	}

	/* this is a dangerous fallback*/
	if (wakeup_signum == -1) {
		wakeup_signum = SIGUSR1;
		sigaction(wakeup_signum, &sa, NULL);
	}
}

/**
 * os_thread_create - spawn a new host operating system thread
 * @func: the entry point routine for the new thread
 * @data: argument passed to the thread entry routine
 *
 * return: a pointer to the thread handle, or an ERR_PTR on failure.
 */
struct os_thread *os_thread_create(os_thread_func_t func, void *data)
{
	struct os_thread *thread;
	int ret;

	thread = calloc(1, sizeof(*thread));
	if (!thread)
		return ERR_PTR(-ENOMEM);

	ret = pthread_create(&thread->handle, NULL, func, data);
	if (ret != 0) {
		free(thread);
		return ERR_PTR(-EAGAIN);
	}

	return thread;
}

/**
 * os_thread_join - wait for a host operating system thread to terminate
 * @thread: the thread handle to wait upon
 *
 * Blocks the calling thread until the specified thread finishes execution.
 * This is crucial during hypervisor teardown to ensure all vCPUs have
 * fully exited before freeing shared memory resources.
 *
 * return: 0 on successful join, or -EINVAL if the thread is invalid.
 */
int os_thread_join(struct os_thread *thread)
{
	if (!thread)
		return -EINVAL;

	if (pthread_join(thread->handle, NULL) != 0)
		return -EINVAL;

	return 0;
}

/**
 * os_thread_send_wakeup - asynchronously interrupt a thread
 * @thread: the target thread to wake up
 *
 * Sends the dynamically allocated system wakeup signal to the target thread.
 * This is primarily used to break a vCPU thread out of a blocking KVM_RUN
 * ioctl during system shutdown or hardware interrupt injection.
 */
void os_thread_send_wakeup(struct os_thread *thread)
{
	if (thread && wakeup_signum != -1)
		pthread_kill(thread->handle, wakeup_signum);
}

/**
 * os_thread_block_wakeup - mask the wakeup signal for the current thread
 *
 * Blocks the wakeup signal at the host OS scheduler level. vCPU threads MUST
 * call this immediately upon entry to prevent consuming the signal in userspace
 * before transferring control to the hardware virtualization extensions.
 */
void os_thread_block_wakeup(void)
{
	sigset_t mask;

	if (wakeup_signum == -1)
		return;

	sigemptyset(&mask);
	sigaddset(&mask, wakeup_signum);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);
}

/**
 * os_thread_fill_wakeup_sigmask - generate the signal mask for virtual processors.
 * @buf: destination buffer for the raw signal mask bits.
 * @size: expected length of the kernel signal mask.
 *
 * return: 0 on success, or a negative error code.
 */
int os_thread_fill_wakeup_sigmask(void *buf, size_t size)
{
	sigset_t unblocked;

	if (wakeup_signum == -1)
		return -EINVAL;

	pthread_sigmask(SIG_SETMASK, NULL, &unblocked);
	sigdelset(&unblocked, wakeup_signum);

	memcpy(buf, &unblocked, size);
	return 0;
}

/**
 * os_thread_destroy - free the memory associated with a thread handle
 * @thread: the thread handle to destroy
 *
 * Note that this does not terminate the running thread; it merely frees the
 * tracking structure. Ensure os_thread_join() is called prior to this.
 */
void os_thread_destroy(struct os_thread *thread)
{
	free(thread);
}

/**
 * os_mutex_create - allocate and initialize a mutual exclusion lock
 *
 * Creates a standard, non-recursive POSIX mutex. Used to serialize access
 * to emulated hardware state (like UART registers) from multiple concurrently
 * running vCPU threads.
 *
 * return: a pointer to the newly allocated mutex, or an ERR_PTR on memory exhaustion.
 */
struct os_mutex *os_mutex_create(void)
{
	struct os_mutex *mutex;

	mutex = calloc(1, sizeof(*mutex));
	if (!mutex)
		return ERR_PTR(-ENOMEM);

	if (pthread_mutex_init(&mutex->handle, NULL) != 0) {
		free(mutex);
		return ERR_PTR(-ENOMEM);
	}

	return mutex;
}

/**
 * os_mutex_lock - acquire the mutual exclusion lock
 * @mutex: the mutex to acquire
 *
 * Blocks the current thread until the mutex becomes available.
 * Deadlocks will occur if a thread attempts to lock a mutex it already holds.
 */
void os_mutex_lock(struct os_mutex *mutex)
{
	if (mutex)
		pthread_mutex_lock(&mutex->handle);
}

/**
 * os_mutex_unlock - release the mutual exclusion lock
 * @mutex: the mutex to release
 *
 * Unlocks the mutex, potentially waking up other threads blocked in os_mutex_lock().
 */
void os_mutex_unlock(struct os_mutex *mutex)
{
	if (mutex)
		pthread_mutex_unlock(&mutex->handle);
}

/**
 * os_mutex_destroy - tear down a mutex and free its resources
 * @mutex: the mutex to destroy
 *
 * The mutex must be unlocked and no threads should be blocked on it
 * before calling this function.
 */
void os_mutex_destroy(struct os_mutex *mutex)
{
	if (mutex) {
		pthread_mutex_destroy(&mutex->handle);
		free(mutex);
	}
}