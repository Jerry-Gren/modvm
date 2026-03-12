#ifndef MODVM_LOG_H
#define MODVM_LOG_H

#include <modvm/utils/compiler.h>
#include <modvm/utils/once_lite.h>

enum modvm_log_level {
	MODVM_LOG_EMERG = 0,
	MODVM_LOG_ALERT,
	MODVM_LOG_CRIT,
	MODVM_LOG_ERR,
	MODVM_LOG_WARN,
	MODVM_LOG_NOTICE,
	MODVM_LOG_INFO,
	MODVM_LOG_DEBUG,
};

/**
 * pr_fmt - used by the pr_*() macros to generate the modvm_log format string
 * @fmt: format string passed from a pr_*() macro
 *
 * This macro can be used to generate a unified format string for pr_*()
 * macros. A common use is to prefix all pr_*() messages in a file with a common
 * string. For example, defining this at the top of a source file:
 *
 *        #define pr_fmt(fmt) MODNAME ": " fmt
 *
 * would prefix all pr_info, pr_emerg... messages in the file with the module
 * name.
 */
#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

int modvm_log_initialize(void);
void modvm_log_destroy(void);

int modvm_log(enum modvm_log_level level, const char *fmt, ...);
void modvm_panic(const char *fmt, ...) __noreturn __cold;

/**
 * pr_emerg - print an emergency-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a modvm_log with MODVM_LOG_EMERG loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_emerg(fmt, ...) \
	modvm_log(MODVM_LOG_EMERG, pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_alert - print an alert-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a modvm_log with MODVM_LOG_ALERT loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_alert(fmt, ...) \
	modvm_log(MODVM_LOG_ALERT, pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_crit - print a critical-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a modvm_log with MODVM_LOG_CRIT loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_crit(fmt, ...) modvm_log(MODVM_LOG_CRIT, pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_err - print an error-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a modvm_log with MODVM_LOG_ERR loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_err(fmt, ...) modvm_log(MODVM_LOG_ERR, pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_warn - print a warning-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a modvm_log with MODVM_LOG_WARNING loglevel. It uses pr_fmt()
 * to generate the format string.
 */
#define pr_warn(fmt, ...) modvm_log(MODVM_LOG_WARN, pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_notice - print a notice-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a modvm_log with MODVM_LOG_NOTICE loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_notice(fmt, ...) \
	modvm_log(MODVM_LOG_NOTICE, pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_info - print an info-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a modvm_log with MODVM_LOG_INFO loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_info(fmt, ...) modvm_log(MODVM_LOG_INFO, pr_fmt(fmt), ##__VA_ARGS__)

/**
 * pr_debug - print a debug-level message conditionally
 * @fmt: format string
 * @...: arguments for the format string
 *
 * If DEBUG is defined, it's equivalent to a modvm_log with
 * MODVM_LOG_DEBUG loglevel. If DEBUG is not defined it does nothing.
 *
 * It uses pr_fmt() to generate the format string (dynamic_pr_debug() uses
 * pr_fmt() internally).
 */
#ifdef DEBUG
#define pr_debug(fmt, ...) \
	modvm_log(MODVM_LOG_DEBUG, pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...)                                      \
	do {                                                    \
		if (0)                                          \
			modvm_log(MODVM_LOG_DEBUG, pr_fmt(fmt), \
				  ##__VA_ARGS__);               \
	} while (0)
#endif

/*
 * print a one-time message (analogous to WARN_ONCE() et al):
 */

#define pr_emerg_once(fmt, ...) DO_ONCE_LITE(pr_emerg, fmt, ##__VA_ARGS__)
#define pr_alert_once(fmt, ...) DO_ONCE_LITE(pr_alert, fmt, ##__VA_ARGS__)
#define pr_crit_once(fmt, ...) DO_ONCE_LITE(pr_crit, fmt, ##__VA_ARGS__)
#define pr_err_once(fmt, ...) DO_ONCE_LITE(pr_err, fmt, ##__VA_ARGS__)
#define pr_warn_once(fmt, ...) DO_ONCE_LITE(pr_warn, fmt, ##__VA_ARGS__)
#define pr_notice_once(fmt, ...) DO_ONCE_LITE(pr_notice, fmt, ##__VA_ARGS__)
#define pr_info_once(fmt, ...) DO_ONCE_LITE(pr_info, fmt, ##__VA_ARGS__)

#ifdef DEBUG
#define pr_debug_once(fmt, ...) DO_ONCE_LITE(pr_debug, fmt, ##__VA_ARGS__)
#else
#define pr_debug_once(fmt, ...)                       \
	do {                                          \
		if (0)                                \
			pr_debug(fmt, ##__VA_ARGS__); \
	} while (0)
#endif

#endif /* MODVM_LOG_H */