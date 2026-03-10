/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include <modvm/utils/log.h>
#include <modvm/os/thread.h>

/* Don't do this:
 *
 * #undef pr_fmt
 * #define pr_fmt(fmt) "log: " fmt
 */

/* Standard buffer size for a single log line, typical in kernel environments */
#define LOG_LINE_MAX_LENGTH 1024

/* Mapping log levels to standard colorized output or specific prefixes */
static const char *const level_prefixes[] = {
	[VM_LOG_EMERG] = "[EMERG]  ", [VM_LOG_ALERT] = "[ALERT]  ",
	[VM_LOG_CRIT] = "[CRIT]   ",  [VM_LOG_ERR] = "[ERROR]  ",
	[VM_LOG_WARN] = "[WARN]   ",  [VM_LOG_NOTICE] = "[NOTICE] ",
	[VM_LOG_INFO] = "[INFO]   ",  [VM_LOG_DEBUG] = "[DEBUG]  ",
};

static struct os_mutex *global_logging_mutex = NULL;

/**
 * vm_log_initialize - initialize the global logging subsystem
 *
 * Allocates the synchronization primitive required to prevent log tearing
 * in symmetric multiprocessing environments. This must be invoked explicitly
 * by the bootstrap processor before any concurrent threads are spawned.
 *
 * return: 0 upon successful initialization, or a negative error code.
 */
int vm_log_initialize(void)
{
	if (!global_logging_mutex) {
		global_logging_mutex = os_mutex_create();
		if (!global_logging_mutex) {
			/* * Fallback to unbuffered stderr if we cannot even allocate a lock 
             * during early boot phase.
             */
			fprintf(stderr,
				"[CRIT]   failed to initialize global logging mutex\n");
			return -ENOMEM;
		}
	}
	return 0;
}

/**
 * vm_log_destroy - tear down the global logging subsystem
 *
 * Releases the synchronization primitive. Any concurrent logging attempts
 * after this point may result in undefined behavior.
 */
void vm_log_destroy(void)
{
	if (global_logging_mutex) {
		os_mutex_destroy(global_logging_mutex);
		global_logging_mutex = NULL;
	}
}

/**
 * vm_log - emits a formatted message to the standard output streams
 * @level: the severity level of the message determining its output target
 * @format_string: the format specification string
 *
 * Assembles the severity prefix and the payload into a local stack buffer.
 * The final output loop is protected by a mutual exclusion lock to ensure
 * atomic character emission, preventing interlaced text output when multiple
 * virtual processors execute logging routines simultaneously.
 *
 * return: the total number of characters processed.
 */
int vm_log(enum vm_log_level level, const char *format_string, ...)
{
	va_list variadic_arguments;
	int formatting_result;
	int prefix_length = 0;
	int payload_length;
	int available_capacity;
	int byte_index;
	char text_buffer[LOG_LINE_MAX_LENGTH];
	FILE *output_stream = (level <= VM_LOG_ERR) ? stderr : stdout;

	if (level <= VM_LOG_DEBUG) {
		prefix_length = snprintf(text_buffer, sizeof(text_buffer), "%s",
					 level_prefixes[level]);
		if (prefix_length < 0) {
			prefix_length = 0;
		} else if (prefix_length >= (int)sizeof(text_buffer)) {
			prefix_length = sizeof(text_buffer) - 1;
		}
	}

	available_capacity = sizeof(text_buffer) - prefix_length;
	if (available_capacity > 0) {
		va_start(variadic_arguments, format_string);
		formatting_result = vsnprintf(text_buffer + prefix_length,
					      available_capacity, format_string,
					      variadic_arguments);
		va_end(variadic_arguments);

		if (formatting_result < 0) {
			/*
			 * Encountered an encoding error during string formatting.
			 * Discard the payload but retain the prefix.
			 */
			payload_length = 0;
		} else if (formatting_result >= available_capacity) {
			/*
			 * The formatted output exceeded the buffer capacity.
			 * The vsnprintf function returns the projected length, not the
			 * actual bytes written. We must strictly clamp the length to
			 * the buffer boundary minus the null terminator to prevent
			 * a stack out-of-bounds read vulnerability.
			 */
			payload_length = available_capacity - 1;
		} else {
			payload_length = formatting_result;
		}
	} else {
		payload_length = 0;
	}

	/*
	 * We conditionally lock here. If the subsystem hasn't been initialized yet
	 * (e.g., extremely early boot errors), we still attempt to print, risking
	 * log tearing rather than losing critical diagnostics.
	 */
	if (global_logging_mutex) {
		os_mutex_lock(global_logging_mutex);
	}

	/*
	 * Output byte-by-byte to securely inject carriage returns, shielding
	 * the host terminal against staircase rendering in raw mode.
	 */
	for (byte_index = 0; byte_index < prefix_length + payload_length;
	     byte_index++) {
		if (text_buffer[byte_index] == '\n')
			fputc('\r', output_stream);
		fputc(text_buffer[byte_index], output_stream);
	}
	fflush(output_stream);

	if (global_logging_mutex) {
		os_mutex_unlock(global_logging_mutex);
	}

	return prefix_length + payload_length;
}

/**
 * vm_panic - abort the virtualization engine on unrecoverable errors
 * @format_string: the descriptive format string explaining the fatality
 *
 * This routine is decorated with the noreturn attribute. It flushes a final
 * diagnostic message directly to standard error and halts the host process.
 */
void vm_panic(const char *format_string, ...)
{
	va_list variadic_arguments;
	char text_buffer[LOG_LINE_MAX_LENGTH];
	int prefix_length;
	int available_capacity;
	int byte_index;

	prefix_length = snprintf(text_buffer, sizeof(text_buffer),
				 "\r\n[MODVM PANIC] ");
	if (prefix_length < 0) {
		prefix_length = 0;
	} else if (prefix_length >= (int)sizeof(text_buffer)) {
		prefix_length = sizeof(text_buffer) - 1;
	}

	available_capacity = sizeof(text_buffer) - prefix_length;
	if (available_capacity > 0) {
		va_start(variadic_arguments, format_string);
		/*
		 * vsnprintf inherently null-terminates the buffer
		 * even upon truncation. Since we iterate based on the null byte below,
		 * capturing the return value is unnecessary here.
		 */
		vsnprintf(text_buffer + prefix_length, available_capacity,
			  format_string, variadic_arguments);
		va_end(variadic_arguments);
	}

	if (global_logging_mutex) {
		os_mutex_lock(global_logging_mutex);
	}

	for (byte_index = 0; text_buffer[byte_index] != '\0'; byte_index++) {
		if (text_buffer[byte_index] == '\n')
			fputc('\r', stderr);
		fputc(text_buffer[byte_index], stderr);
	}

	fputs("\r\nSystem halted.\r\n", stderr);
	fflush(stderr);

	if (global_logging_mutex) {
		os_mutex_unlock(global_logging_mutex);
	}

	abort();
}