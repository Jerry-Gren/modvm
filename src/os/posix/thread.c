/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#include <modvm/os/thread.h>
#include <modvm/utils/err.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

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
 * os_thread_system_init - scan and allocate a non-colliding RT signal
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

	if (unlikely(wakeup_signum == -1)) {
		wakeup_signum = SIGUSR1;
		sigaction(wakeup_signum, &sa, NULL);
	}
}

/**
 * os_thread_create - spawn a posix thread wrapper
 * @func: execution entry point
 * @data: closure data
 *
 * Return: allocated thread handle, or ERR_PTR on failure.
 */
struct os_thread *os_thread_create(os_thread_func_t func, void *data)
{
	struct os_thread *thread;
	int ret;

	if (WARN_ON(!func))
		return ERR_PTR(-EINVAL);

	thread = calloc(1, sizeof(*thread));
	if (unlikely(!thread))
		return ERR_PTR(-ENOMEM);

	ret = pthread_create(&thread->handle, NULL, func, data);
	if (unlikely(ret != 0)) {
		free(thread);
		return ERR_PTR(-EAGAIN);
	}

	return thread;
}

/**
 * os_thread_join - block until thread termination
 * @thread: handle to block against
 *
 * Return: 0 on success, or -EINVAL if thread is invalid.
 */
int os_thread_join(struct os_thread *thread)
{
	if (WARN_ON(!thread))
		return -EINVAL;

	if (unlikely(pthread_join(thread->handle, NULL) != 0))
		return -EINVAL;

	return 0;
}

/**
 * os_thread_send_wakeup - kick a blocking vcpu from hardware mode
 * @thread: target hardware processor thread
 */
void os_thread_send_wakeup(struct os_thread *thread)
{
	if (likely(thread && wakeup_signum != -1))
		pthread_kill(thread->handle, wakeup_signum);
}

/**
 * os_thread_block_wakeup - mask dynamic signal against current scheduler frame
 */
void os_thread_block_wakeup(void)
{
	sigset_t mask;

	if (unlikely(wakeup_signum == -1))
		return;

	sigemptyset(&mask);
	sigaddset(&mask, wakeup_signum);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);
}

/**
 * os_thread_fill_wakeup_sigmask - expose routing unblocked state
 * @buf: buffer to accept bitmask
 * @size: expected posix sigset byte width
 *
 * Return: 0 on success.
 */
int os_thread_fill_wakeup_sigmask(void *buf, size_t size)
{
	sigset_t unblocked;

	if (WARN_ON(!buf))
		return -EINVAL;

	if (unlikely(wakeup_signum == -1))
		return -EINVAL;

	pthread_sigmask(SIG_SETMASK, NULL, &unblocked);
	sigdelset(&unblocked, wakeup_signum);

	memcpy(buf, &unblocked, size);
	return 0;
}

/**
 * os_thread_destroy - free os container tracker
 * @thread: thread tracking block
 */
void os_thread_destroy(struct os_thread *thread)
{
	free(thread);
}

struct os_mutex *os_mutex_create(void)
{
	struct os_mutex *mutex;

	mutex = calloc(1, sizeof(*mutex));
	if (unlikely(!mutex))
		return ERR_PTR(-ENOMEM);

	if (unlikely(pthread_mutex_init(&mutex->handle, NULL) != 0)) {
		free(mutex);
		return ERR_PTR(-ENOMEM);
	}

	return mutex;
}

void os_mutex_lock(struct os_mutex *mutex)
{
	if (likely(mutex))
		pthread_mutex_lock(&mutex->handle);
}

void os_mutex_unlock(struct os_mutex *mutex)
{
	if (likely(mutex))
		pthread_mutex_unlock(&mutex->handle);
}

void os_mutex_destroy(struct os_mutex *mutex)
{
	if (likely(mutex)) {
		pthread_mutex_destroy(&mutex->handle);
		free(mutex);
	}
}