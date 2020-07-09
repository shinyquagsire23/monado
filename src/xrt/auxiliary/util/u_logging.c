// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Logging functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_logging.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>


/*
 *
 * Helper functions.
 *
 */

static void
print_prefix(const char *func, enum u_logging_level level)
{
	switch (level) {
	case U_LOGGING_TRACE: fprintf(stderr, "TRACE "); break;
	case U_LOGGING_DEBUG: fprintf(stderr, "DEBUG "); break;
	case U_LOGGING_INFO: fprintf(stderr, "INFO "); break;
	case U_LOGGING_WARN: fprintf(stderr, "WARN "); break;
	case U_LOGGING_ERROR: fprintf(stderr, "ERROR "); break;
	case U_LOGGING_RAW: break;
	default: break;
	}

	if (level != U_LOGGING_RAW && func != NULL) {
		fprintf(stderr, "[%s] ", func);
	}
}


/*
 *
 * 'Exported' functions.
 *
 */

void
u_log(const char *file,
      int line,
      const char *func,
      enum u_logging_level level,
      const char *format,
      ...)
{
	print_prefix(func, level);

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	fprintf(stderr, "\n");
}

void
u_log_xdev(const char *file,
           int line,
           const char *func,
           enum u_logging_level level,
           struct xrt_device *xdev,
           const char *format,
           ...)
{
	print_prefix(func, level);

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	fprintf(stderr, "\n");
}
