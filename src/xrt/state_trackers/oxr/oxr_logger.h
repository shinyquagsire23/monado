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
XrResult
oxr_error(struct oxr_logger *logger, XrResult result, const char *fmt, ...)
    XRT_PRINTF_FORMAT(3, 4);

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
