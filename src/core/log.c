/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <modvm/log.h>

/* Don't do this:
 *
 * #undef pr_fmt
 * #define pr_fmt(fmt) "log: " fmt
 */

/* Standard buffer size for a single log line, typical in kernel environments */
#define LOG_LINE_MAX_LEN 1024

/* Mapping log levels to standard colorized output or specific prefixes */
static const char *const level_prefixes[] = {
	[VM_LOG_EMERG] = "[EMERG]  ", [VM_LOG_ALERT] = "[ALERT]  ",
	[VM_LOG_CRIT] = "[CRIT]   ",  [VM_LOG_ERR] = "[ERROR]  ",
	[VM_LOG_WARN] = "[WARN]   ",  [VM_LOG_NOTICE] = "[NOTICE] ",
	[VM_LOG_INFO] = "[INFO]   ",  [VM_LOG_DEBUG] = "[DEBUG]  ",
};

/**
 * vm_log - the logging router
 * @level: the severity level of the message
 * @fmt: the format string
 *
 * Assembles the log prefix and the message into a local stack buffer
 * to ensure that the entire log line is emitted atomically via a single
 * standard I/O call. This prevents log tearing in SMP environments.
 *
 * Return: The number of bytes written, or a negative value on failure.
 */
int vm_log(enum vm_log_level level, const char *fmt, ...)
{
	va_list args;
	int r;
	int offset = 0;
	int written;
	int max_len;
	int i;
	char buf[LOG_LINE_MAX_LEN];
	FILE *stream = (level <= VM_LOG_ERR) ? stderr : stdout;

	if (level <= VM_LOG_DEBUG) {
		offset =
			snprintf(buf, sizeof(buf), "%s", level_prefixes[level]);
		if (offset < 0) {
			offset = 0;
		} else if (offset >= (int)sizeof(buf)) {
			offset = sizeof(buf) - 1;
		}
	}

	max_len = sizeof(buf) - offset;
	if (max_len > 0) {
		va_start(args, fmt);
		r = vsnprintf(buf + offset, max_len, fmt, args);
		va_end(args);

		if (r < 0) {
			/*
			 * Encountered an encoding error during string formatting.
			 * Discard the payload but retain the prefix.
			 */
			written = 0;
		} else if (r >= max_len) {
			/*
			 * The formatted output exceeded the buffer capacity.
			 * The vsnprintf function returns the projected length, not the
			 * actual bytes written. We must strictly clamp the length to
			 * the buffer boundary minus the null terminator to prevent
			 * a stack out-of-bounds read vulnerability.
			 */
			written = max_len - 1;
		} else {
			written = r;
		}
	} else {
		written = 0;
	}

	/*
	 * Output byte-by-byte to securely inject carriage returns, shielding
	 * the host terminal against staircase rendering in raw mode.
	 */
	for (i = 0; i < offset + written; i++) {
		if (buf[i] == '\n')
			fputc('\r', stream);
		fputc(buf[i], stream);
	}

	fflush(stream);

	return offset + written;
}

/**
 * vm_panic - the termination routine
 * @fmt: the format string describing the fatal error
 */
void vm_panic(const char *fmt, ...)
{
	va_list args;
	char buf[LOG_LINE_MAX_LEN];
	int offset;
	int max_len;
	int i;

	offset = snprintf(buf, sizeof(buf), "\r\n[MODVM PANIC] ");
	if (offset < 0) {
		offset = 0;
	} else if (offset >= (int)sizeof(buf)) {
		offset = sizeof(buf) - 1;
	}

	max_len = sizeof(buf) - offset;
	if (max_len > 0) {
		va_start(args, fmt);
		/*
		 * vsnprintf inherently null-terminates the buffer
		 * even upon truncation. Since we iterate based on the null byte below,
		 * capturing the return value is unnecessary here.
		 */
		vsnprintf(buf + offset, max_len, fmt, args);
		va_end(args);
	}

	for (i = 0; buf[i] != '\0'; i++) {
		if (buf[i] == '\n')
			fputc('\r', stderr);
		fputc(buf[i], stderr);
	}

	fputs("\r\nSystem halted.\r\n", stderr);
	fflush(stderr);

	abort();
}