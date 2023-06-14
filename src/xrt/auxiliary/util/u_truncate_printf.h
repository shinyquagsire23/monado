// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Truncating versions of string printing functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_compiler.h"

#include <stdio.h>
#include <stdarg.h>
#include <limits.h>


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * We want to truncate the value, not get the possible written.
 *
 * There are no version of the *many* Windows versions of this functions that
 * truncates and returns the number of bytes written (not including null). Also
 * need to have the same behaviour on Linux.
 *
 * @ingroup @aux_util
 */
static inline int
u_truncate_vsnprintf(char *chars, size_t char_count, const char *fmt, va_list args)
{
	/*
	 * We always want to be able to write null terminator, and
	 * something propbly went wrong if char_count larger then INT_MAX.
	 */
	if (char_count == 0 || char_count > INT_MAX) {
		return -1;
	}

	// Will always be able to write null terminator.
	int ret = vsnprintf(chars, char_count, fmt, args);
	if (ret < 0) {
		return ret;
	}

	// Safe, ret is checked for negative above.
	if ((size_t)ret > char_count - 1) {
		return (int)char_count - 1;
	}

	return ret;
}

/*!
 * We want to truncate the value, not get the possible written, and error when
 * we can not write out anything.
 *
 * See @ref u_truncate_vsnprintf for more info.
 *
 * @ingroup @aux_util
 */
static inline int
u_truncate_snprintf(char *chars, size_t char_count, const char *fmt, ...)
{
	/*
	 * We always want to be able to write null terminator, and
	 * something propbly went wrong if char_count larger then INT_MAX.
	 */
	if (char_count == 0 || char_count > INT_MAX) {
		return -1;
	}

	va_list args;
	va_start(args, fmt);
	int ret = u_truncate_vsnprintf(chars, char_count, fmt, args);
	va_end(args);

	return ret;
}


#ifdef __cplusplus
}
#endif
