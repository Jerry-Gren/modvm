/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

#include <modvm/os_thread.h>
#include <modvm/err.h>

/**
 * struct os_thread - POSIX specific thread container
 * @handle: the underlying pthread identifier
 */
struct os_thread {
	pthread_t handle;
};

static int system_kick_signal = -1;

static void empty_signal_handler(int signum)
{
	/* Intentionally empty. We only need the signal to interrupt ioctl(). */
	(void)signum;
}

void os_thread_system_init(void)
{
	struct sigaction sa;
	struct sigaction old_sa;
	int sig;

	if (system_kick_signal != -1)
		return;

	sa.sa_handler = empty_signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	/*
	 * Scan the POSIX Real-Time signal range for an unused slot.
	 * This prevents hostile takeover of standard signals like SIGUSR1,
	 * ensuring the hypervisor engine remains a polite resident when
	 * embedded within larger host applications.
	 */
	for (sig = SIGRTMIN; sig <= SIGRTMAX; sig++) {
		if (sigaction(sig, NULL, &old_sa) == 0) {
			/* Claim the signal if it retains default or ignored behavior */
			if (old_sa.sa_handler == SIG_DFL ||
			    old_sa.sa_handler == SIG_IGN) {
				if (sigaction(sig, &sa, NULL) == 0) {
					system_kick_signal = sig;
					break;
				}
			}
		}
	}

	/* Fallback to SIGUSR1 if no RT signals are available */
	if (system_kick_signal == -1) {
		system_kick_signal = SIGUSR1;
		sigaction(system_kick_signal, &sa, NULL);
	}
}

struct os_thread *os_thread_create(os_thread_fn func, void *opaque)
{
	struct os_thread *thread;
	int ret;

	thread = calloc(1, sizeof(*thread));
	if (!thread)
		return ERR_PTR(-ENOMEM);

	ret = pthread_create(&thread->handle, NULL, func, opaque);
	if (ret != 0) {
		free(thread);
		return ERR_PTR(-EAGAIN);
	}

	return thread;
}

int os_thread_join(struct os_thread *thread)
{
	int ret;

	if (!thread)
		return -EINVAL;

	ret = pthread_join(thread->handle, NULL);
	if (ret != 0)
		return -EINVAL;

	return 0;
}

void os_thread_kick(struct os_thread *thread)
{
	if (thread && system_kick_signal != -1)
		pthread_kill(thread->handle, system_kick_signal);
}

void os_thread_mask_kick_signal(void)
{
	sigset_t set;

	if (system_kick_signal == -1)
		return;

	sigemptyset(&set);
	sigaddset(&set, system_kick_signal);

	/*
	 * Block the dynamically allocated kick signal in the current thread.
	 * This ensures the signal remains pending and is not consumed
	 * prematurely while the thread is executing in userspace.
	 */
	pthread_sigmask(SIG_BLOCK, &set, NULL);
}

int os_thread_get_kick_signal(void)
{
	return system_kick_signal;
}

void os_thread_destroy(struct os_thread *thread)
{
	if (thread)
		free(thread);
}

/**
 * struct os_mutex - POSIX specific mutex container
 * @handle: the underlying pthread mutex identifier
 */
struct os_mutex {
	pthread_mutex_t handle;
};

struct os_mutex *os_mutex_create(void)
{
	struct os_mutex *mutex;
	int ret;

	mutex = calloc(1, sizeof(*mutex));
	if (!mutex)
		return ERR_PTR(-ENOMEM);

	ret = pthread_mutex_init(&mutex->handle, NULL);
	if (ret != 0) {
		free(mutex);
		return ERR_PTR(-ENOMEM);
	}

	return mutex;
}

void os_mutex_lock(struct os_mutex *mutex)
{
	if (mutex)
		pthread_mutex_lock(&mutex->handle);
}

void os_mutex_unlock(struct os_mutex *mutex)
{
	if (mutex)
		pthread_mutex_unlock(&mutex->handle);
}

void os_mutex_destroy(struct os_mutex *mutex)
{
	if (mutex) {
		pthread_mutex_destroy(&mutex->handle);
		free(mutex);
	}
}