// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Basic logging functionality.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_log
 */


#pragma once

#include "xrt/xrt_compiler.h"


#ifdef __cplusplus
extern "C" {
#endif


struct xrt_device;


/*!
 * @defgroup aux_log Logging functions.
 * @ingroup aux_util
 */

/*!
 * @ingroup aux_log
 * @{
 */

/*!
 * For places where you really just want printf, prints a new-line.
 */
#define U_LOG_RAW(...)                                                                                                 \
	do {                                                                                                           \
		u_log(__FILE__, __LINE__, __func__, U_LOGGING_RAW, __VA_ARGS__);                                       \
	} while (false)

#define U_LOG(level, ...)                                                                                              \
	do {                                                                                                           \
		u_log(__FILE__, __LINE__, __func__, level, __VA_ARGS__);                                               \
	} while (false)

#define U_LOG_IFL(level, cond_level, ...)                                                                              \
	do {                                                                                                           \
		if (cond_level <= level) {                                                                             \
			u_log(__FILE__, __LINE__, __func__, level, __VA_ARGS__);                                       \
		}                                                                                                      \
	} while (false)

#define U_LOG_XDEV(level, xdev, ...)                                                                                   \
	do {                                                                                                           \
		u_log_xdev(__FILE__, __LINE__, __func__, level, xdev, __VA_ARGS__);                                    \
	} while (false)

#define U_LOG_XDEV_IFL(level, cond_level, xdev, ...)                                                                   \
	do {                                                                                                           \
		if (cond_level <= level) {                                                                             \
			u_log_xdev(__FILE__, __LINE__, __func__, level, xdev, __VA_ARGS__);                            \
		}                                                                                                      \
	} while (false)

// clang-format off
#define U_LOG_T(...) U_LOG_IFL_T(global_log_level, __VA_ARGS__)
#define U_LOG_D(...) U_LOG_IFL_D(global_log_level, __VA_ARGS__)
#define U_LOG_I(...) U_LOG_IFL_I(global_log_level, __VA_ARGS__)
#define U_LOG_W(...) U_LOG_IFL_W(global_log_level, __VA_ARGS__)
#define U_LOG_E(...) U_LOG_IFL_E(global_log_level, __VA_ARGS__)

#define U_LOG_IFL_T(cond_level, ...) U_LOG_IFL(U_LOGGING_TRACE, cond_level, __VA_ARGS__)
#define U_LOG_IFL_D(cond_level, ...) U_LOG_IFL(U_LOGGING_DEBUG, cond_level, __VA_ARGS__)
#define U_LOG_IFL_I(cond_level, ...) U_LOG_IFL(U_LOGGING_INFO, cond_level, __VA_ARGS__)
#define U_LOG_IFL_W(cond_level, ...) U_LOG_IFL(U_LOGGING_WARN, cond_level, __VA_ARGS__)
#define U_LOG_IFL_E(cond_level, ...) U_LOG_IFL(U_LOGGING_ERROR, cond_level, __VA_ARGS__)

#define U_LOG_XDEV_IFL_T(xdev, cond_level, ...) U_LOG_XDEV_IFL(U_LOGGING_TRACE, cond_level, xdev, __VA_ARGS__)
#define U_LOG_XDEV_IFL_D(xdev, cond_level, ...) U_LOG_XDEV_IFL(U_LOGGING_DEBUG, cond_level, xdev, __VA_ARGS__)
#define U_LOG_XDEV_IFL_I(xdev, cond_level, ...) U_LOG_XDEV_IFL(U_LOGGING_INFO, cond_level, xdev, __VA_ARGS__)
#define U_LOG_XDEV_IFL_W(xdev, cond_level, ...) U_LOG_XDEV_IFL(U_LOGGING_WARN, cond_level, xdev, __VA_ARGS__)
#define U_LOG_XDEV_IFL_E(xdev, cond_level, ...) U_LOG_XDEV_IFL(U_LOGGING_ERROR, cond_level, xdev, __VA_ARGS__)

#define U_LOG_XDEV_T(xdev, ...) U_LOG_XDEV(U_LOGGING_TRACE, xdev, __VA_ARGS__)
#define U_LOG_XDEV_D(xdev, ...) U_LOG_XDEV(U_LOGGING_DEBUG, xdev, __VA_ARGS__)
#define U_LOG_XDEV_I(xdev, ...) U_LOG_XDEV(U_LOGGING_INFO, xdev, __VA_ARGS__)
#define U_LOG_XDEV_W(xdev, ...) U_LOG_XDEV(U_LOGGING_WARN, xdev, __VA_ARGS__)
#define U_LOG_XDEV_E(xdev, ...) U_LOG_XDEV(U_LOGGING_ERROR, xdev, __VA_ARGS__)
// clang-format on

enum u_logging_level
{
	U_LOGGING_TRACE,
	U_LOGGING_DEBUG,
	U_LOGGING_INFO,
	U_LOGGING_WARN,
	U_LOGGING_ERROR,
	U_LOGGING_RAW, //!< Special level for raw printing, prints a new-line.
};

extern enum u_logging_level global_log_level;

void
u_log(const char *file, int line, const char *func, enum u_logging_level level, const char *format, ...)
    XRT_PRINTF_FORMAT(5, 6);

void
u_log_xdev(const char *file,
           int line,
           const char *func,
           enum u_logging_level level,
           struct xrt_device *xdev,
           const char *format,
           ...) XRT_PRINTF_FORMAT(6, 7);


/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
