// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small debug helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 *
 * Debug get option helpers heavily inspired from mesa ones.
 */

#include "util/u_debug.h"
#include "util/u_logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


DEBUG_GET_ONCE_BOOL_OPTION(print, "XRT_PRINT_OPTIONS", false)

static const char *
os_getenv(const char *name)
{
	return getenv(name);
}

const char *
debug_get_option(const char *name, const char *_default)
{
	const char *raw = getenv(name);
	const char *ret;

	if (raw == NULL) {
		ret = _default;
	} else {
		ret = raw;
	}

	if (debug_get_bool_option_print()) {
		U_LOG_RAW("%s=%s (%s)", name, ret, raw == NULL ? "nil" : raw);
	}

	return ret;
}

bool
debug_get_bool_option(const char *name, bool _default)
{
	const char *raw = os_getenv(name);
	bool ret;

	if (raw == NULL) {
		ret = _default;
	} else if (!strcmp(raw, "false")) {
		ret = false;
	} else if (!strcmp(raw, "FALSE")) {
		ret = false;
	} else if (!strcmp(raw, "off")) {
		ret = false;
	} else if (!strcmp(raw, "OFF")) {
		ret = false;
	} else if (!strcmp(raw, "no")) {
		ret = false;
	} else if (!strcmp(raw, "NO")) {
		ret = false;
	} else if (!strcmp(raw, "n")) {
		ret = false;
	} else if (!strcmp(raw, "N")) {
		ret = false;
	} else if (!strcmp(raw, "f")) {
		ret = false;
	} else if (!strcmp(raw, "F")) {
		ret = false;
	} else if (!strcmp(raw, "0")) {
		ret = false;
	} else {
		ret = true;
	}

	if (debug_get_bool_option_print()) {
		U_LOG_RAW("%s=%s (%s)", name, ret ? "TRUE" : "FALSE",
		          raw == NULL ? "nil" : raw);
	}

	return ret;
}

long
debug_get_num_option(const char *name, long _default)
{
	const char *raw = os_getenv(name);
	long ret;

	if (raw == NULL) {
		ret = _default;
	} else {
		char *endptr;

		ret = strtol(raw, &endptr, 0);
		// Restore the default value when no digits were found.
		if (raw == endptr) {
			ret = _default;
		}
	}

	if (debug_get_bool_option_print()) {
		U_LOG_RAW("%s=%li (%s)", name, ret, raw == NULL ? "nil" : raw);
	}

	return ret;
}

float
debug_get_float_option(const char *name, float _default)
{
	const char *raw = os_getenv(name);
	float ret;

	if (raw == NULL) {
		ret = _default;
	} else {
		char *endptr;

		ret = strtof(raw, &endptr);
		// Restore the default value when no digits were found.
		if (raw == endptr) {
			ret = _default;
		}
	}

	if (debug_get_bool_option_print()) {
		U_LOG_RAW("%s=%f (%s)", name, ret, raw == NULL ? "nil" : raw);
	}

	return ret;
}
