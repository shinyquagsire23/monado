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
#include "xrt/xrt_config_build.h"

#include "util/u_debug.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

DEBUG_GET_ONCE_LOG_OPTION(global_log, "XRT_LOG", U_LOGGING_WARN)

enum u_logging_level
u_log_get_global_level(void)
{
	return debug_get_log_option_global_log();
}

// Logging sink global data.
static u_log_sink_func_t g_log_sink_func;
static void *g_log_sink_data;

void
u_log_set_sink(u_log_sink_func_t func, void *data)
{
	g_log_sink_func = func;
	g_log_sink_data = data;
}

#define DISPATCH_SINK(FILE, LINE, FUNC, LEVEL, FORMAT, ARGS)                                                           \
	if (g_log_sink_func != NULL) {                                                                                 \
		va_list copy;                                                                                          \
		va_copy(copy, ARGS);                                                                                   \
		g_log_sink_func(FILE, LINE, FUNC, LEVEL, FORMAT, copy, g_log_sink_data);                               \
		va_end(copy);                                                                                          \
	}

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
u_log(const char *file, int line, const char *func, enum u_logging_level level, const char *format, ...)
{
	// print_prefix(func, level);
	android_LogPriority prio = u_log_convert_priority(level);
	va_list args;
	va_start(args, format);
	DISPATCH_SINK(file, line, func, level, format, args);
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
	DISPATCH_SINK(file, line, func, level, format, args);
	__android_log_vprint(prio, func, format, args);
	va_end(args);
}


#elif defined(XRT_OS_WINDOWS)

#include <debugapi.h>

static int
print_prefix(int remainingBuf, char *buf, const char *func, enum u_logging_level level)
{
	int printed = 0;
	switch (level) {
	case U_LOGGING_TRACE: printed = sprintf_s(buf, remainingBuf, "TRACE "); break;
	case U_LOGGING_DEBUG: printed = sprintf_s(buf, remainingBuf, "DEBUG "); break;
	case U_LOGGING_INFO: printed = sprintf_s(buf, remainingBuf, " INFO "); break;
	case U_LOGGING_WARN: printed = sprintf_s(buf, remainingBuf, " WARN "); break;
	case U_LOGGING_ERROR: printed = sprintf_s(buf, remainingBuf, "ERROR "); break;
	case U_LOGGING_RAW: break;
	default: break;
	}

	if (level != U_LOGGING_RAW && func != NULL) {
		printed += sprintf_s(buf + printed, remainingBuf - printed, "[%s] ", func);
	}
	return printed;
}

void
u_log(const char *file, int line, const char *func, enum u_logging_level level, const char *format, ...)
{

	char buf[16384] = {0};

	int remainingBuffer = sizeof(buf) - 2; // 2 for \n\0
	int printed = print_prefix(remainingBuffer, buf, func, level);

	va_list args;
	va_start(args, format);
	DISPATCH_SINK(file, line, func, level, format, args);
	printed += vsprintf_s(buf + printed, remainingBuffer - printed, format, args);
	va_end(args);
	buf[printed++] = '\n';
	buf[printed++] = '\0';
	OutputDebugStringA(buf);
	fprintf(stderr, "%s", buf);
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

	int remainingBuffer = sizeof(buf) - 2; // 2 for \n\0
	int printed = print_prefix(remainingBuffer, buf, func, level);

	va_list args;
	va_start(args, format);
	DISPATCH_SINK(file, line, func, level, format, args);
	printed += vsprintf_s(buf + printed, remainingBuffer - printed, format, args);
	va_end(args);
	buf[printed++] = '\n';
	buf[printed++] = '\0';
	OutputDebugStringA(buf);
	fprintf(stderr, "%s", buf);
}


#else

#include <unistd.h>

/*
 *
 * Helper functions.
 *
 */


#ifdef XRT_FEATURE_COLOR_LOG
#define COLOR_TRACE "\033[2m"
#define COLOR_DEBUG "\033[36m"
#define COLOR_INFO "\033[32m"
#define COLOR_WARN "\033[33m"
#define COLOR_ERROR "\033[31m"
#define COLOR_RESET "\033[0m"

static void
print_prefix_color(const char *func, enum u_logging_level level)
{
	switch (level) {
	case U_LOGGING_TRACE: fprintf(stderr, COLOR_TRACE "TRACE " COLOR_RESET); break;
	case U_LOGGING_DEBUG: fprintf(stderr, COLOR_DEBUG "DEBUG " COLOR_RESET); break;
	case U_LOGGING_INFO: fprintf(stderr, COLOR_INFO " INFO " COLOR_RESET); break;
	case U_LOGGING_WARN: fprintf(stderr, COLOR_WARN " WARN " COLOR_RESET); break;
	case U_LOGGING_ERROR: fprintf(stderr, COLOR_ERROR "ERROR " COLOR_RESET); break;
	case U_LOGGING_RAW: break;
	default: break;
	}
}
#endif

static void
print_prefix_mono(const char *func, enum u_logging_level level)
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
}

static void
print_prefix(const char *func, enum u_logging_level level)
{
#ifdef XRT_FEATURE_COLOR_LOG
	if (isatty(STDERR_FILENO)) {
		print_prefix_color(func, level);
	} else {
		print_prefix_mono(func, level);
	}
#else
	print_prefix_mono(func, level);
#endif

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
u_log(const char *file, int line, const char *func, enum u_logging_level level, const char *format, ...)
{
	print_prefix(func, level);

	va_list args;
	va_start(args, format);
	DISPATCH_SINK(file, line, func, level, format, args);
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
	DISPATCH_SINK(file, line, func, level, format, args);
	vfprintf(stderr, format, args);
	va_end(args);

	fprintf(stderr, "\n");
}
#endif
