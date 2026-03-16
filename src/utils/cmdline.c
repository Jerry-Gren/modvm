/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <modvm/utils/cmdline.h>
#include <modvm/utils/bug.h>

/**
 * cmdline_extract_opt - parse a key-value string option
 * @opts: comma-separated option string (e.g., "kernel=/path,append=foo")
 * @key: the key to search for
 *
 * Return: dynamically allocated string containing the value, or NULL if not found.
 * The caller is strictly responsible for freeing the returned string.
 */
char *cmdline_extract_opt(const char *opts, const char *key)
{
	const char *start;
	const char *end;
	char *val;
	size_t len;
	char search_key[32];

	if (WARN_ON(!opts || !key))
		return NULL;

	snprintf(search_key, sizeof(search_key), "%s=", key);
	start = strstr(opts, search_key);
	if (!start)
		return NULL;

	start += strlen(search_key);

	/* The 'append' key often contains commas for kernel args, treat it specially */
	if (strcmp(key, "append") == 0) {
		end = start + strlen(start);
	} else {
		end = strchr(start, ',');
		if (!end)
			end = start + strlen(start);
	}

	len = end - start;
	val = malloc(len + 1);
	if (val) {
		strncpy(val, start, len);
		val[len] = '\0';
	}
	return val;
}