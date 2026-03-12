/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/err.h>
#include <modvm/utils/compiler.h>
#include <modvm/os/thread.h>

/* Don't do this:
 *
 * #undef pr_fmt
 * #define pr_fmt(fmt) "log: " fmt
 */

#define LOG_LINE_MAX_LENGTH 1024

static const char *const level_prefixes[] = {
	[MODVM_LOG_EMERG] = "[EMERG]  ", [MODVM_LOG_ALERT] = "[ALERT]  ",
	[MODVM_LOG_CRIT] = "[CRIT]   ",	 [MODVM_LOG_ERR] = "[ERROR]  ",
	[MODVM_LOG_WARN] = "[WARN]   ",	 [MODVM_LOG_NOTICE] = "[NOTICE] ",
	[MODVM_LOG_INFO] = "[INFO]   ",	 [MODVM_LOG_DEBUG] = "[DEBUG]  ",
};

static struct os_mutex *log_lock = NULL;

/**
 * modvm_log_initialize - initialize the global logging subsystem
 *
 * Allocates the synchronization primitive required to prevent log tearing
 * in symmetric multiprocessing environments. Must be invoked early.
 *
 * Return: 0 upon successful initialization, or a negative error code.
 */
int modvm_log_initialize(void)
{
	if (!log_lock) {
		log_lock = os_mutex_create();
		if (IS_ERR(log_lock)) {
			fprintf(stderr, "[CRIT]   failed to init log mutex\n");
			log_lock = NULL;
			return -ENOMEM;
		}
	}
	return 0;
}

/**
 * modvm_log_destroy - tear down the global logging subsystem
 */
void modvm_log_destroy(void)
{
	if (log_lock) {
		os_mutex_destroy(log_lock);
		log_lock = NULL;
	}
}

/**
 * modvm_log - emits a formatted message to the standard output streams
 * @level: the severity level of the message determining its output target
 * @fmt: the format specification string
 *
 * Assembles the severity prefix and the payload into a local stack buffer.
 * Protected by a mutual exclusion lock to ensure atomic character emission.
 *
 * Return: the total number of characters processed.
 */
int modvm_log(enum modvm_log_level level, const char *fmt, ...)
{
	va_list args;
	int ret;
	int prefix_len = 0;
	int payload_len;
	int avail;
	int i;
	char buf[LOG_LINE_MAX_LENGTH];
	FILE *stream = (level <= MODVM_LOG_ERR) ? stderr : stdout;

	if (level <= MODVM_LOG_DEBUG) {
		prefix_len =
			snprintf(buf, sizeof(buf), "%s", level_prefixes[level]);
		if (unlikely(prefix_len < 0))
			prefix_len = 0;
		else if (unlikely(prefix_len >= (int)sizeof(buf)))
			prefix_len = sizeof(buf) - 1;
	}

	avail = sizeof(buf) - prefix_len;
	if (likely(avail > 0)) {
		va_start(args, fmt);
		ret = vsnprintf(buf + prefix_len, avail, fmt, args);
		va_end(args);

		if (unlikely(ret < 0)) {
			/*
			 * Encountered an encoding error during string formatting.
			 * Discard the payload but retain the prefix.
			 */
			payload_len = 0;
		} else if (unlikely(ret >= avail)) {
			/*
			 * The formatted output exceeded the buffer capacity.
			 * The vsnprintf function returns the projected length, not the
			 * actual bytes written. We must strictly clamp the length to
			 * the buffer boundary minus the null terminator to prevent
			 * a stack out-of-bounds read vulnerability.
			 */
			payload_len = avail - 1;
		} else {
			payload_len = ret;
		}
	} else {
		payload_len = 0;
	}

	/*
	 * We conditionally lock here. If the subsystem hasn't been initialized yet
	 * (e.g., extremely early boot errors), we still attempt to print, risking
	 * log tearing rather than losing critical diagnostics.
	 */
	if (likely(log_lock))
		os_mutex_lock(log_lock);

	/*
	 * Output byte-by-byte to securely inject carriage returns, shielding
	 * the host terminal against staircase rendering in raw mode.
	 */
	for (i = 0; i < prefix_len + payload_len; i++) {
		if (buf[i] == '\n')
			fputc('\r', stream);
		fputc(buf[i], stream);
	}
	fflush(stream);

	if (likely(log_lock))
		os_mutex_unlock(log_lock);

	return prefix_len + payload_len;
}

/**
 * modvm_panic - abort the virtualization engine on unrecoverable errors
 * @fmt: the descriptive format string explaining the fatality
 *
 * Flushes a final diagnostic message directly to standard error and halts
 * the host process. Decorated with the noreturn attribute.
 */
void modvm_panic(const char *fmt, ...)
{
	va_list args;
	char buf[LOG_LINE_MAX_LENGTH];
	int prefix_len;
	int avail;
	int i;

	prefix_len = snprintf(buf, sizeof(buf), "\r\n[MODVM PANIC] ");
	if (prefix_len < 0)
		prefix_len = 0;
	else if (prefix_len >= (int)sizeof(buf))
		prefix_len = sizeof(buf) - 1;

	avail = sizeof(buf) - prefix_len;
	if (avail > 0) {
		va_start(args, fmt);
		/*
		 * vsnprintf inherently null-terminates the buffer
		 * even upon truncation. Since we iterate based on the null byte below,
		 * capturing the return value is unnecessary here.
		 */
		vsnprintf(buf + prefix_len, avail, fmt, args);
		va_end(args);
	}

	if (log_lock)
		os_mutex_lock(log_lock);

	for (i = 0; buf[i] != '\0'; i++) {
		if (buf[i] == '\n')
			fputc('\r', stderr);
		fputc(buf[i], stderr);
	}

	fputs("\r\nSystem halted.\r\n", stderr);
	fflush(stderr);

	if (log_lock)
		os_mutex_unlock(log_lock);

	abort();
}