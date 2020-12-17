// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Logging functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_logging.h"
#include "xrt/xrt_config_os.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>


#if defined(XRT_OS_ANDROID)

#include <android/log.h>

static android_LogPriority
u_log_convert_priority(enum u_logging_level level)
{
	switch (level) {
	case U_LOGGING_TRACE: return ANDROID_LOG_VERBOSE;
	case U_LOGGING_DEBUG: return ANDROID_LOG_DEBUG;
	case U_LOGGING_INFO: return ANDROID_LOG_INFO;
	case U_LOGGING_WARN: return ANDROID_LOG_WARN;
	case U_LOGGING_ERROR: return ANDROID_LOG_ERROR;
	case U_LOGGING_RAW: return ANDROID_LOG_INFO;
	default: break;
	}
	return ANDROID_LOG_INFO;
}
void
u_log(const char *file,
      int line,
      const char *func,
      enum u_logging_level level,
      const char *format,
      ...)
{
	// print_prefix(func, level);
	android_LogPriority prio = u_log_convert_priority(level);
	va_list args;
	va_start(args, format);
	__android_log_vprint(prio, func, format, args);
	va_end(args);
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
	android_LogPriority prio = u_log_convert_priority(level);
	va_list args;
	va_start(args, format);
	__android_log_vprint(prio, func, format, args);
	va_end(args);
}


#elif defined(XRT_OS_WINDOWS)

#include <debugapi.h>

static int
print_prefix(int remainingBuf,
             char *buf,
             const char *func,
             enum u_logging_level level)
{
	int printed = 0;
	switch (level) {
	case U_LOGGING_TRACE:
		printed = sprintf_s(buf, remainingBuf, "TRACE ");
		break;
	case U_LOGGING_DEBUG:
		printed = sprintf_s(buf, remainingBuf, "DEBUG ");
		break;
	case U_LOGGING_INFO:
		printed = sprintf_s(buf, remainingBuf, " INFO ");
		break;
	case U_LOGGING_WARN:
		printed = sprintf_s(buf, remainingBuf, " WARN ");
		break;
	case U_LOGGING_ERROR:
		printed = sprintf_s(buf, remainingBuf, "ERROR ");
		break;
	case U_LOGGING_RAW: break;
	default: break;
	}

	if (level != U_LOGGING_RAW && func != NULL) {
		printed = sprintf_s(buf + printed, remainingBuf - printed,
		                    "[%s] ", func);
	}
	return printed;
}

void
u_log(const char *file,
      int line,
      const char *func,
      enum u_logging_level level,
      const char *format,
      ...)
{
	char buf[16384] = {0};

	int remainingBuffer = sizeof(buf) - 2;
	int printed = print_prefix(remainingBuffer, buf, func, level);

	va_list args;
	va_start(args, format);
	printed +=
	    vsprintf_s(buf + printed, remainingBuffer - printed, format, args);
	va_end(args);
	*(buf + printed) = '\n';
	OutputDebugStringA(buf);
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
	char buf[16384] = {0};

	int remainingBuffer = sizeof(buf) - 1;
	int printed = print_prefix(remainingBuffer, buf, func, level);

	va_list args;
	va_start(args, format);
	vsprintf_s(buf + printed, remainingBuffer - printed, format, args);
	va_end(args);
	OutputDebugStringA(buf);
}


#else
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
	case U_LOGGING_INFO: fprintf(stderr, " INFO "); break;
	case U_LOGGING_WARN: fprintf(stderr, " WARN "); break;
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
#endif
