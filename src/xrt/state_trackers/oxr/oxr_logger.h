// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Logging functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Helper macro to log a warning just once.
 *
 * @ingroup oxr_main
 */
#define OXR_WARN_ONCE(log, ...)                                                \
	do {                                                                   \
		static bool _once = false;                                     \
		if (!_once) {                                                  \
			_once = true;                                          \
			oxr_warn(log, __VA_ARGS__);                            \
		}                                                              \
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
 * @ingroup oxr_main
 * @{
 */

void
oxr_log_init(struct oxr_logger *logger, const char *api_func_name);
void
oxr_log_set_instance(struct oxr_logger *logger, struct oxr_instance *inst);
void
oxr_log(struct oxr_logger *logger, const char *fmt, ...)
    XRT_PRINTF_FORMAT(2, 3);
void
oxr_warn(struct oxr_logger *logger, const char *fmt, ...)
    XRT_PRINTF_FORMAT(2, 3);

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
oxr_error(struct oxr_logger *logger, XrResult result, const char *fmt, ...)
    XRT_PRINTF_FORMAT(3, 4);

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
