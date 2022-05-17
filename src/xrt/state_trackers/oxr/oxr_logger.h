// Copyright 2018-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Logging functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#pragma once

#include "util/u_pretty_print.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Helper macro to log a warning just once.
 *
 * @ingroup oxr_main
 */
#define OXR_WARN_ONCE(log, ...)                                                                                        \
	do {                                                                                                           \
		static bool _once = false;                                                                             \
		if (!_once) {                                                                                          \
			_once = true;                                                                                  \
			oxr_warn(log, __VA_ARGS__);                                                                    \
		}                                                                                                      \
	} while (false)

/*!
 * Logger struct that lives on the stack, one for each call client call.
 *
 * @ingroup oxr_main
 */
struct oxr_logger
{
	struct oxr_instance *inst;
	const char *api_func_name;
};


/*!
 * @addtogroup oxr_main
 * @{
 */

void
oxr_log_init(struct oxr_logger *logger, const char *api_func_name);
void
oxr_log_set_instance(struct oxr_logger *logger, struct oxr_instance *inst);
void
oxr_log(struct oxr_logger *logger, const char *fmt, ...) XRT_PRINTF_FORMAT(2, 3);
void
oxr_warn(struct oxr_logger *logger, const char *fmt, ...) XRT_PRINTF_FORMAT(2, 3);

/*!
 * Output an error and return the result code.
 *
 * Intended for use in a return statement, to log error information and return
 * the result code in a single line.
 *
 * Note: The format string is appended to the function name with no spaces,
 * so it should either start with a parenthesized argument name followed by a
 * space and the message, or should start with a space then the message.
 * That is, a format string of `"(arg) info"` becomes `XR_ERROR: xrFunc(arg)
 * info`, and a format string of `" info msg"` becomes `XR_ERROR: xrFunc info
 * msg`.
 */
XrResult
oxr_error(struct oxr_logger *logger, XrResult result, const char *fmt, ...) XRT_PRINTF_FORMAT(3, 4);



/*
 *
 * Sink logger.
 *
 */

/*!
 * Allocate on the stack, make sure to zero initialize.
 */
struct oxr_sink_logger
{
	char *store;
	size_t store_size;
	size_t length;
};

/*!
 * Log string to sink logger.
 */
void
oxr_slog(struct oxr_sink_logger *slog, const char *fmt, ...) XRT_PRINTF_FORMAT(2, 3);

/*!
 * Add the string to the slog struct.
 */
void
oxr_slog_add_array(struct oxr_sink_logger *slog, const char *str, size_t size);

/*!
 * Get a pretty print delegate from a @ref oxr_sink_logger.
 */
static inline u_pp_delegate_t
oxr_slog_dg(struct oxr_sink_logger *slog)
{
	u_pp_delegate_t dg = {(void *)slog, (u_pp_delegate_func_t)oxr_slog_add_array};
	return dg;
}

/*!
 * Cancel logging, frees all internal data.
 */
void
oxr_slog_cancel(struct oxr_sink_logger *slog);

/*!
 * Flush sink as a log message, frees all internal data.
 */
void
oxr_log_slog(struct oxr_logger *log, struct oxr_sink_logger *slog);

/*!
 * Flush sink as a warning message, frees all internal data.
 */
void
oxr_warn_slog(struct oxr_logger *log, struct oxr_sink_logger *slog);

/*!
 * Flush sink as a error message, frees all internal data.
 */
XrResult
oxr_error_slog(struct oxr_logger *log, XrResult res, struct oxr_sink_logger *slog);


/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
