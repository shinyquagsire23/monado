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
#include "util/u_truncate_printf.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>


/*
 *
 * Defines.
 *
 */

/*
 * Avoid 4K stack variables.
 * https://learn.microsoft.com/en-us/windows/win32/devnotes/-win32-chkstk
 */
#define LOG_BUFFER_SIZE (3 * 1024)


/*
 *
 * Global log level functions.
 *
 */

DEBUG_GET_ONCE_LOG_OPTION(global_log, "XRT_LOG", U_LOGGING_WARN)

enum u_logging_level
u_log_get_global_level(void)
{
	return debug_get_log_option_global_log();
}


/*
 *
 * Logging sink.
 *
 */

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


/*
 *
 * Hexdump functions.
 *
 */

static void
u_log_hexdump_line(char *buf, size_t offset, const uint8_t *data, size_t data_size)
{
	char *pos = buf;

	if (data_size > 16) {
		data_size = 16;
	}

	pos += sprintf(pos, "%08x: ", (uint32_t)offset);

	char *ascii = pos + (16 * 3) + 1;
	size_t i;

	for (i = 0; i < data_size; i++) {
		pos += sprintf(pos, "%02x ", data[i]);

		if (data[i] >= ' ' && data[i] <= '~') {
			*ascii++ = data[i];
		} else {
			*ascii++ = '.';
		}
	}

	/* Pad short lines with spaces, and null terminate */
	while (i++ < 16) {
		pos += sprintf(pos, "   ");
	}
	/* Replace the first NULL terminator with a space */
	*pos++ = ' ';
	/* and set it after the ASCII representation */
	*ascii++ = '\0';
}

void
u_log_hex(const char *file,
          int line,
          const char *func,
          enum u_logging_level level,
          const uint8_t *data,
          const size_t data_size)
{
	size_t offset = 0;

	while (offset < data_size) {
		char tmp[128];
		u_log_hexdump_line(tmp, offset, data + offset, data_size - offset);
		u_log(file, line, func, level, "%s", tmp);

		offset += 16;
		/*
		 * Limit the dump length to 16MB, this used to be 4GB which
		 * would on 32bit system always evaltuate to false. So we have
		 * the limit on something more sensible.
		 */
		if (offset > 0x00ffffff) {
			u_log(file, line, func, level, "Truncating output over 16MB");
			break;
		}
	}
}

void
u_log_xdev_hex(const char *file,
               int line,
               const char *func,
               enum u_logging_level level,
               struct xrt_device *xdev,
               const uint8_t *data,
               const size_t data_size)
{
	size_t offset = 0;

	while (offset < data_size) {
		char tmp[128];
		u_log_hexdump_line(tmp, offset, data + offset, data_size - offset);
		u_log_xdev(file, line, func, level, xdev, "%s", tmp);

		offset += 16;
		/*
		 * Limit the dump length to 16MB, this used to be 4GB which
		 * would on 32bit system always evaltuate to false. So we have
		 * the limit on something more sensible.
		 */
		if (offset > 0x00ffffff) {
			u_log_xdev(file, line, func, level, xdev, "Truncating output over 16MB");
			break;
		}
	}
}


/*
 *
 * Platform specific functions.
 *
 */

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

#elif defined(XRT_OS_WINDOWS)

#include <debugapi.h>

#else

#include <unistd.h>

#endif


/*
 *
 * Helper functions.
 *
 */

#define CHECK_RET_AND_UPDATE_STATE()                                                                                   \
	do {                                                                                                           \
		if (ret < 0) {                                                                                         \
			return ret;                                                                                    \
		}                                                                                                      \
                                                                                                                       \
		printed += ret; /* Is not negative, checked above. */                                                  \
		remaining -= ret;                                                                                      \
		buf += ret; /* Continue in the buffer from where we left off. */                                       \
	} while (false);

#ifdef XRT_FEATURE_COLOR_LOG
#define COLOR_TRACE "\033[2m"
#define COLOR_DEBUG "\033[36m"
#define COLOR_INFO "\033[32m"
#define COLOR_WARN "\033[33m"
#define COLOR_ERROR "\033[31m"
#define COLOR_RESET "\033[0m"

static int
print_prefix_color(const char *func, enum u_logging_level level, char *buf, int remaining)
{
	switch (level) {
	case U_LOGGING_TRACE: return u_truncate_snprintf(buf, remaining, COLOR_TRACE "TRACE " COLOR_RESET);
	case U_LOGGING_DEBUG: return u_truncate_snprintf(buf, remaining, COLOR_DEBUG "DEBUG " COLOR_RESET);
	case U_LOGGING_INFO: return u_truncate_snprintf(buf, remaining, COLOR_INFO " INFO " COLOR_RESET);
	case U_LOGGING_WARN: return u_truncate_snprintf(buf, remaining, COLOR_WARN " WARN " COLOR_RESET);
	case U_LOGGING_ERROR: return u_truncate_snprintf(buf, remaining, COLOR_ERROR "ERROR " COLOR_RESET);
	case U_LOGGING_RAW: return 0;
	default: return 0;
	}
}
#endif

static int
print_prefix_mono(const char *func, enum u_logging_level level, char *buf, int remaining)
{
	switch (level) {
	case U_LOGGING_TRACE: return u_truncate_snprintf(buf, remaining, "TRACE ");
	case U_LOGGING_DEBUG: return u_truncate_snprintf(buf, remaining, "DEBUG ");
	case U_LOGGING_INFO: return u_truncate_snprintf(buf, remaining, " INFO ");
	case U_LOGGING_WARN: return u_truncate_snprintf(buf, remaining, " WARN ");
	case U_LOGGING_ERROR: return u_truncate_snprintf(buf, remaining, "ERROR ");
	case U_LOGGING_RAW: return 0;
	default: return 0;
	}
}

static int
print_prefix(const char *func, enum u_logging_level level, char *buf, int remaining)
{
	int printed = 0;
	int ret = 0;

#ifdef XRT_FEATURE_COLOR_LOG
	if (isatty(STDERR_FILENO)) {
		ret = print_prefix_color(func, level, buf, remaining);
	} else {
		ret = print_prefix_mono(func, level, buf, remaining);
	}
#else
	ret = print_prefix_mono(func, level, buf, remaining);
#endif

	// Does what it says.
	CHECK_RET_AND_UPDATE_STATE();

	// Print the function name.
	if (level != U_LOGGING_RAW && func != NULL) {
		ret = u_truncate_snprintf(buf, remaining, "[%s] ", func);
	}

	// Does what it says.
	CHECK_RET_AND_UPDATE_STATE();

	// Total printed characters.
	return printed;
}

static int
do_print(const char *file, int line, const char *func, enum u_logging_level level, const char *format, va_list args)
{
	char storage[LOG_BUFFER_SIZE];

	int remaining = sizeof(storage) - 2; // 2 for \n\0
	int printed = 0;
	char *buf = storage; // We update the pointer.
	int ret = 0;

	// The prefix of the log.
	ret = print_prefix(func, level, buf, remaining);

	// Does what it says.
	CHECK_RET_AND_UPDATE_STATE();

	ret = u_truncate_vsnprintf(buf, remaining, format, args);

	// Does what it says.
	CHECK_RET_AND_UPDATE_STATE();

	/*
	 * The variable storage now holds the entire null-terminated message,
	 * but without a new-line character, proceed to output it.
	 */
	assert(storage[printed] == '\0');


#ifdef XRT_OS_ANDROID

	android_LogPriority prio = u_log_convert_priority(level);
	__android_log_write(prio, func, storage);

#elif defined XRT_OS_WINDOWS || defined XRT_OS_LINUX

#if defined XRT_OS_WINDOWS
	OutputDebugStringA(storage); // No newline.
#endif

	// We want a newline, so add it, then null-terminate again.
	storage[printed++] = '\n';
	storage[printed] = '\0'; // Don't count zero termination as printed.

	fwrite(storage, printed, 1, stderr);

#else
#error "Port needed for logging function"
#endif

	return printed;
}


/*
 *
 * 'Exported' functions.
 *
 */

void
u_log(const char *file, int line, const char *func, enum u_logging_level level, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	DISPATCH_SINK(file, line, func, level, format, args);
	do_print(file, line, func, level, format, args);
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
	va_list args;
	va_start(args, format);
	DISPATCH_SINK(file, line, func, level, format, args);
	do_print(file, line, func, level, format, args);
	va_end(args);
}
