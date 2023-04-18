// Copyright 2018-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Logging functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "xrt/xrt_compiler.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"

#include "openxr/openxr_reflection.h"

#include <stdio.h>
#include <stdarg.h>
#include <limits.h>


#define LOG_BUFFER_SIZE (1024)

#ifdef XRT_OS_WINDOWS
#define DEFAULT_NO_STDERR (true)
#define CHECK_SHOULD_NOT_PRINT (debug_get_bool_option_no_printing_stderr())
#else
#define DEFAULT_NO_STDERR (false)
#define CHECK_SHOULD_NOT_PRINT (debug_get_bool_option_no_printing() || debug_get_bool_option_no_printing_stderr())
#endif

DEBUG_GET_ONCE_BOOL_OPTION(no_printing, "OXR_NO_PRINTING", false)
DEBUG_GET_ONCE_BOOL_OPTION(no_printing_stderr, "OXR_NO_PRINTING_STDERR", DEFAULT_NO_STDERR)
DEBUG_GET_ONCE_BOOL_OPTION(entrypoints, "OXR_DEBUG_ENTRYPOINTS", false)
DEBUG_GET_ONCE_BOOL_OPTION(break_on_error, "OXR_BREAK_ON_ERROR", false)


/*
 *
 * Helpers
 *
 */

static const char *
oxr_result_to_string(XrResult result);

static bool
is_fmt_func_arg_start(const char *fmt)
{
	if (fmt == NULL) {
		return false;
	}
	if (fmt[0] == '(') {
		return true;
	}
	return false;
}

/*!
 * We want to truncate the value, not get the possible written.
 *
 * There are no version of the *many* Windows versions of this functions that
 * truncates and returns the number of bytes written (not including null).
 */
static int
truncate_vsnprintf(char *chars, size_t char_count, const char *fmt, va_list args)
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
 * We want to truncate the value, not get the possible written.
 */
static int
truncate_snprintf(char *chars, size_t char_count, const char *fmt, ...)
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
	int ret = truncate_vsnprintf(chars, char_count, fmt, args);
	va_end(args);
	return ret;
}

/*!
 * Prints the first part of a logging message, has three forms.
 *
 * ```c++
 * print_prefix(l, "(myInfo->memberName) is bad", "XR_ERROR_VALIDATION_FAILURE");
 * // XR_ERROR_VALIDATION_FAILURE: xrMyFunc(myInfo->memberName) is bad
 *
 * print_prefix(l, "This is bad", "XR_ERROR_VALIDATION_FAILURE");
 * // XR_ERROR_VALIDATION_FAILURE in xrMyFunc: This is bad
 *
 * print_prefix(l, "No functions set now", "LOG");
 * // LOG: No function set now
 * ```
 */
static int
print_prefix(struct oxr_logger *logger, const char *fmt, const char *prefix, char *buf, int remaining)
{
	if (logger->api_func_name != NULL) {
		if (is_fmt_func_arg_start(fmt)) {
			return truncate_snprintf(buf, remaining, "%s: %s", prefix, logger->api_func_name);
		} else {
			return truncate_snprintf(buf, remaining, "%s in %s: ", prefix, logger->api_func_name);
		}
	} else {
		return truncate_snprintf(buf, remaining, "%s: ", prefix);
	}
}

static void
do_output(const char *buf)
{
#ifdef XRT_OS_WINDOWS
	OutputDebugStringA(buf);

	if (debug_get_bool_option_no_printing_stderr()) {
		return;
	}
#endif

	fprintf(stderr, "%s", buf);
}

static void
do_print(struct oxr_logger *logger, const char *fmt, const char *prefix, va_list args)
{
	char buf[LOG_BUFFER_SIZE];

	int remaining = sizeof(buf) - 2; // 2 for \n\0
	int printed = 0;
	int ret;

	ret = print_prefix(logger, fmt, prefix, buf, remaining);
	if (ret < 0) {
		U_LOG_E("Internal OpenXR logging error!");
		return;
	}
	printed += ret;

	ret = truncate_vsnprintf(buf + printed, remaining - printed, fmt, args);
	if (ret < 0) {
		U_LOG_E("Internal OpenXR logging error!");
		return;
	}
	printed += ret;

	// Always add newline.
	buf[printed++] = '\n';
	buf[printed++] = '\0';

	do_output(buf);
}

static void
do_print_func(const char *api_func_name)
{
	char buf[LOG_BUFFER_SIZE];
	truncate_snprintf(buf, sizeof(buf), "%s\n", api_func_name);
	do_output(buf);
}


/*
 *
 * 'Exported' functions.
 *
 */

void
oxr_log_init(struct oxr_logger *logger, const char *api_func_name)
{
	if (debug_get_bool_option_entrypoints()) {
		do_print_func(api_func_name);
	}

	logger->inst = NULL;
	logger->api_func_name = api_func_name;
}

void
oxr_log_set_instance(struct oxr_logger *logger, struct oxr_instance *inst)
{
	logger->inst = inst;
}

void
oxr_log(struct oxr_logger *logger, const char *fmt, ...)
{
	if (CHECK_SHOULD_NOT_PRINT) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	do_print(logger, fmt, "LOG", args);
	va_end(args);
}

void
oxr_warn(struct oxr_logger *logger, const char *fmt, ...)
{
	if (CHECK_SHOULD_NOT_PRINT) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	do_print(logger, fmt, "WARNING", args);
	va_end(args);
}

XrResult
oxr_error(struct oxr_logger *logger, XrResult result, const char *fmt, ...)
{
	if (CHECK_SHOULD_NOT_PRINT) {
		return result;
	}

	va_list args;
	va_start(args, fmt);
	do_print(logger, fmt, oxr_result_to_string(result), args);
	va_end(args);

	if (debug_get_bool_option_break_on_error() && result != XR_ERROR_FUNCTION_UNSUPPORTED) {
		/// Trigger a debugger breakpoint.
		XRT_DEBUGBREAK();
	}

	return result;
}

static const char *
oxr_result_to_string(XrResult result)
{
	// clang-format off
	switch (result) {

#define ENTRY(NAME, VALUE) \
	case VALUE: return #NAME;
	XR_LIST_ENUM_XrResult(ENTRY)
#undef ENTRY

	default: return "<UNKNOWN>";
	}
	// clang-format on
}


/*
 *
 * Sink logger.
 *
 */

static void
oxr_slog_ensure(struct oxr_sink_logger *slog, size_t extra)
{
	while (slog->store_size < extra + slog->length) {
		slog->store_size += 1024;
	}

	U_ARRAY_REALLOC_OR_FREE(slog->store, char, slog->store_size);
}

static void
slog_free_store(struct oxr_sink_logger *slog)
{
	free(slog->store);
	slog->length = 0;
	slog->store_size = 0;
}

void
oxr_slog(struct oxr_sink_logger *slog, const char *fmt, ...)
{
	va_list args;

	int ret = 0;

	va_start(args, fmt);
	ret = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	if (ret <= 0) {
		return;
	}

	oxr_slog_ensure(slog, ret + 1);

	va_start(args, fmt);
	ret = vsnprintf(slog->store + slog->length, slog->store_size - slog->length, fmt, args);
	va_end(args);

	slog->length += ret;
}

void
oxr_slog_add_array(struct oxr_sink_logger *slog, const char *str, size_t size)
{
	if (size == 0) {
		return;
	}

	size_t size_with_null = size + 1;
	oxr_slog_ensure(slog, size_with_null);

	memcpy(slog->store + slog->length, str, size);

	slog->length += size;
}

void
oxr_slog_cancel(struct oxr_sink_logger *slog)
{
	slog_free_store(slog);
}

void
oxr_log_slog(struct oxr_logger *log, struct oxr_sink_logger *slog)
{
	oxr_log(log, "%s", slog->store);
	slog_free_store(slog);
}

void
oxr_warn_slog(struct oxr_logger *log, struct oxr_sink_logger *slog)
{
	oxr_warn(log, "%s", slog->store);
	slog_free_store(slog);
}

XrResult
oxr_error_slog(struct oxr_logger *log, XrResult res, struct oxr_sink_logger *slog)
{
	oxr_error(log, res, "%s", slog->store);
	slog_free_store(slog);
	return res;
}
