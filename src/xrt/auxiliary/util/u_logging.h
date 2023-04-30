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

#include <stdarg.h>


#ifdef __cplusplus
extern "C" {
#endif


struct xrt_device;


/*!
 * @defgroup aux_log Logging functions
 * @ingroup aux_util
 */

/*!
 * @addtogroup aux_log
 * @{
 */

/*!
 * @brief Logging level enum
 */
enum u_logging_level
{
	U_LOGGING_TRACE, //!< Trace messages, highly verbose.
	U_LOGGING_DEBUG, //!< Debug messages, verbose.
	U_LOGGING_INFO,  //!< Info messages: not very verbose, not indicating a problem.
	U_LOGGING_WARN,  //!< Warning messages: indicating a potential problem
	U_LOGGING_ERROR, //!< Error messages: indicating a problem
	U_LOGGING_RAW,   //!< Special level for raw printing, prints a new-line.
};

/*!
 * Function typedef for setting the logging sink.
 *
 * @param file   Source file name associated with a message.
 * @param line   Source file line associated with a message.
 * @param func   Function name associated with a message.
 * @param level  Message level: used for formatting or forwarding to native log functions.
 * @param format Format string.
 * @param args   Format parameters.
 * @param data   User data.
 */
typedef void (*u_log_sink_func_t)(const char *file,
                                  int line,
                                  const char *func,
                                  enum u_logging_level level,
                                  const char *format,
                                  va_list args,
                                  void *data);

/*!
 * For places where you really want printf, prints a new-line.
 */
#define U_LOG_RAW(...)                                                                                                 \
	do {                                                                                                           \
		u_log(__FILE__, __LINE__, __func__, U_LOGGING_RAW, __VA_ARGS__);                                       \
	} while (false)

/*!
 * @name Base Logging Utilities
 * In most cases, you will want to use another macro from this file, or a module/driver-local macro, to do your logging.
 * @{
 */
/*!
 * @brief Log a message at @p level , with file, line, and function context (always logs) - typically wrapped
 * in a helper macro.
 *
 * @param level A @ref u_logging_level value for this message.
 * @param ... Format string and optional format arguments.
 */
#define U_LOG(level, ...)                                                                                              \
	do {                                                                                                           \
		u_log(__FILE__, __LINE__, __func__, level, __VA_ARGS__);                                               \
	} while (false)

/*!
 * @brief Log at @p level only if the level is at least @p cond_level - typically wrapped in a helper macro.
 *
 * Adds file, line, and function context. Like U_LOG() but conditional.
 *
 * @param level A @ref u_logging_level value for this message.
 * @param cond_level The minimum @ref u_logging_level that will be actually output.
 * @param ... Format string and optional format arguments.
 */
#define U_LOG_IFL(level, cond_level, ...)                                                                              \
	do {                                                                                                           \
		if (cond_level <= level) {                                                                             \
			u_log(__FILE__, __LINE__, __func__, level, __VA_ARGS__);                                       \
		}                                                                                                      \
	} while (false)
/*!
 * @brief Log at @p level for a given @ref xrt_device - typically wrapped in a helper macro.
 *
 * Adds file, line, and function context, and forwards device context from provided @p xdev .
 *
 * Like U_LOG() but calling u_log_xdev() (which takes a device) instead.
 *
 * @param level A @ref u_logging_level value for this message.
 * @param xdev The @ref xrt_device pointer associated with this message.
 * @param ... Format string and optional format arguments.
 */
#define U_LOG_XDEV(level, xdev, ...)                                                                                   \
	do {                                                                                                           \
		u_log_xdev(__FILE__, __LINE__, __func__, level, xdev, __VA_ARGS__);                                    \
	} while (false)
/*!
 * @brief Log at @p level for a given @ref xrt_device, only if the level is at least @p cond_level - typically wrapped
 * in a helper macro.
 *
 * Adds file, line, and function context, and forwards device context from provided @p xdev .
 * @param level A @ref u_logging_level value for this message.
 * @param cond_level The minimum @ref u_logging_level that will be actually output.
 * @param xdev The @ref xrt_device pointer associated with this message.
 * @param ... Format string and optional format arguments.
 */
#define U_LOG_XDEV_IFL(level, cond_level, xdev, ...)                                                                   \
	do {                                                                                                           \
		if (cond_level <= level) {                                                                             \
			u_log_xdev(__FILE__, __LINE__, __func__, level, xdev, __VA_ARGS__);                            \
		}                                                                                                      \
	} while (false)

/*!
 * @brief Log a memory hexdump at @p level only if the level is at least @p cond_level - typically wrapped in a helper
 * macro.
 *
 * Adds file, line, and function context. Like U_LOG_IFL()
 *
 * @param level A @ref u_logging_level value for this message.
 * @param cond_level The minimum @ref u_logging_level that will be actually output.
 * @param data The data to print in hexdump format
 * @param data_size The size (in bytes) of the data block
 */
#define U_LOG_IFL_HEX(level, cond_level, data, data_size)                                                              \
	do {                                                                                                           \
		if (cond_level <= level) {                                                                             \
			u_log_hex(__FILE__, __LINE__, __func__, level, data, data_size);                               \
		}                                                                                                      \
	} while (false)

/*!
 * @brief Log a memory hexdump at @p level for a given @ref xrt_device, only if the level is at least @p cond_level -
 * typically wrapped in a helper macro.
 *
 * Adds file, line, and function context, and forwards device context from provided @p xdev .
 * @param level A @ref u_logging_level value for this message.
 * @param cond_level The minimum @ref u_logging_level that will be actually output.
 * @param xdev The @ref xrt_device pointer associated with this message.
 * @param data The data to print in hexdump format
 * @param data_size The size (in bytes) of the data block
 */
#define U_LOG_XDEV_IFL_HEX(level, cond_level, xdev, data, data_size)                                                   \
	do {                                                                                                           \
		if (cond_level <= level) {                                                                             \
			u_log_xdev_hex(__FILE__, __LINE__, __func__, level, xdev, data, data_size);                    \
		}                                                                                                      \
	} while (false)


/*!
 * Returns the global logging level, subsystems own logging level take precedence.
 */
enum u_logging_level
u_log_get_global_level(void);

/*!
 * @brief Main non-device-related log implementation function: do not call directly, use a macro that wraps it.
 *
 * This function always logs: level is used for printing or passed to native logging functions.
 *
 * @param file Source file name associated with a message
 * @param line Source file line associated with a message
 * @param func Function name associated with a message
 * @param level Message level: used for formatting or forwarding to native log functions
 * @param format Format string
 * @param ... Format parameters
 */
void
u_log(const char *file, int line, const char *func, enum u_logging_level level, const char *format, ...)
    XRT_PRINTF_FORMAT(5, 6);

/*!
 * @brief Main device-related log implementation function: do not call directly, use a macro that wraps it.
 *
 * This function always logs: level is used for printing or passed to native logging functions.
 * @param file Source file name associated with a message
 * @param line Source file line associated with a message
 * @param func Function name associated with a message
 * @param level Message level: used for formatting or forwarding to native log functions
 * @param xdev The associated @ref xrt_device
 * @param format Format string
 * @param ... Format parameters
 */
void
u_log_xdev(const char *file,
           int line,
           const char *func,
           enum u_logging_level level,
           struct xrt_device *xdev,
           const char *format,
           ...) XRT_PRINTF_FORMAT(6, 7);

/*!
 * @brief Log implementation for dumping memory buffers as hex: do not call directly, use a macro that wraps it.
 *
 * This function always logs: level is used for printing or passed to native logging functions.
 *
 * @param file Source file name associated with a message
 * @param line Source file line associated with a message
 * @param func Function name associated with a message
 * @param level Message level: used for formatting or forwarding to native log functions
 * @param data Data buffer to dump
 * @param data_size Size of the data buffer in bytes
 */
void
u_log_hex(const char *file,
          int line,
          const char *func,
          enum u_logging_level level,
          const uint8_t *data,
          const size_t data_size);

/*!
 * @brief Device-related log implementation for dumping memory buffers as hex: do not call directly, use a macro that
 * wraps it.
 *
 * This function always logs: level is used for printing or passed to native logging functions.
 * @param file Source file name associated with a message
 * @param line Source file line associated with a message
 * @param func Function name associated with a message
 * @param level Message level: used for formatting or forwarding to native log functions
 * @param xdev The associated @ref xrt_device
 * @param data Data buffer to dump
 * @param data_size Size of the data buffer in bytes
 */
void
u_log_xdev_hex(const char *file,
               int line,
               const char *func,
               enum u_logging_level level,
               struct xrt_device *xdev,
               const uint8_t *data,
               const size_t data_size);

/*!
 * Sets the logging sink, log is still passed on to the platform defined output
 * as well as the sink.
 *
 * @param func Logging function for the calls to be sent to.
 * @param data User data to be passed into @p func.
 */
void
u_log_set_sink(u_log_sink_func_t func, void *data);

/*!
 * @}
 */


/*!
 * @name Logging macros conditional on global log level
 *
 * These each imply a log level, and will only log if the global log level is equal or lower.
 * They are often used for one-off logging in a module with few other logging needs,
 * where having a module-specific log level would be unnecessary.
 *
 * @see U_LOG_IFL, u_log_get_global_level()
 * @param ... Format string and optional format arguments.
 * @{
 */
//! Log a message at U_LOGGING_TRACE level, conditional on the global log level
#define U_LOG_T(...) U_LOG_IFL_T(u_log_get_global_level(), __VA_ARGS__)

//! Log a message at U_LOGGING_DEBUG level, conditional on the global log level
#define U_LOG_D(...) U_LOG_IFL_D(u_log_get_global_level(), __VA_ARGS__)

//! Log a message at U_LOGGING_INFO level, conditional on the global log level
#define U_LOG_I(...) U_LOG_IFL_I(u_log_get_global_level(), __VA_ARGS__)

//! Log a message at U_LOGGING_WARN level, conditional on the global log level
#define U_LOG_W(...) U_LOG_IFL_W(u_log_get_global_level(), __VA_ARGS__)

//! Log a message at U_LOGGING_ERROR level, conditional on the global log level
#define U_LOG_E(...) U_LOG_IFL_E(u_log_get_global_level(), __VA_ARGS__)

/*!
 * @}
 */

/*!
 * @name Logging macros conditional on provided log level
 *
 * These are often wrapped within a module, to automatically supply
 * @p cond_level as appropriate for that module.
 *
 * @see U_LOG_IFL
 * @param cond_level The minimum @ref u_logging_level that will be actually output.
 * @param ... Format string and optional format arguments.
 *
 * @{
 */
//! Conditionally log a message at U_LOGGING_TRACE level.
#define U_LOG_IFL_T(cond_level, ...) U_LOG_IFL(U_LOGGING_TRACE, cond_level, __VA_ARGS__)
//! Conditionally log a message at U_LOGGING_DEBUG level.
#define U_LOG_IFL_D(cond_level, ...) U_LOG_IFL(U_LOGGING_DEBUG, cond_level, __VA_ARGS__)
//! Conditionally log a message at U_LOGGING_INFO level.
#define U_LOG_IFL_I(cond_level, ...) U_LOG_IFL(U_LOGGING_INFO, cond_level, __VA_ARGS__)
//! Conditionally log a message at U_LOGGING_WARN level.
#define U_LOG_IFL_W(cond_level, ...) U_LOG_IFL(U_LOGGING_WARN, cond_level, __VA_ARGS__)
//! Conditionally log a message at U_LOGGING_ERROR level.
#define U_LOG_IFL_E(cond_level, ...) U_LOG_IFL(U_LOGGING_ERROR, cond_level, __VA_ARGS__)

//! Conditionally log a memory hexdump at U_LOGGING_TRACE level.
#define U_LOG_IFL_T_HEX(cond_level, data, data_size) U_LOG_IFL_HEX(U_LOGGING_TRACE, cond_level, data, data_size)
//! Conditionally log a memory hexdump at U_LOGGING_DEBUG level.
#define U_LOG_IFL_D_HEX(cond_level, data, data_size) U_LOG_IFL_HEX(U_LOGGING_DEBUG, cond_level, data, data_size)
/*!
 * @}
 */



/*!
 * @name Device-related logging macros conditional on provided log level
 *
 * These are often wrapped within a driver, to automatically supply @p xdev and
 * @p cond_level from their conventional names and log level member variable.
 *
 * @param level A @ref u_logging_level value for this message.
 * @param cond_level The minimum @ref u_logging_level that will be actually output.
 * @param xdev The @ref xrt_device pointer associated with this message.
 * @param ... Format string and optional format arguments.
 *
 * @{
 */
//! Conditionally log a device-related message at U_LOGGING_TRACE level.
#define U_LOG_XDEV_IFL_T(xdev, cond_level, ...) U_LOG_XDEV_IFL(U_LOGGING_TRACE, cond_level, xdev, __VA_ARGS__)
//! Conditionally log a device-related message at U_LOGGING_DEBUG level.
#define U_LOG_XDEV_IFL_D(xdev, cond_level, ...) U_LOG_XDEV_IFL(U_LOGGING_DEBUG, cond_level, xdev, __VA_ARGS__)
//! Conditionally log a device-related message at U_LOGGING_INFO level.
#define U_LOG_XDEV_IFL_I(xdev, cond_level, ...) U_LOG_XDEV_IFL(U_LOGGING_INFO, cond_level, xdev, __VA_ARGS__)
//! Conditionally log a device-related message at U_LOGGING_WARN level.
#define U_LOG_XDEV_IFL_W(xdev, cond_level, ...) U_LOG_XDEV_IFL(U_LOGGING_WARN, cond_level, xdev, __VA_ARGS__)
//! Conditionally log a device-related message at U_LOGGING_ERROR level.
#define U_LOG_XDEV_IFL_E(xdev, cond_level, ...) U_LOG_XDEV_IFL(U_LOGGING_ERROR, cond_level, xdev, __VA_ARGS__)

//! Conditionally log a device-related memory hexdump at U_LOGGING_TRACE level.
#define U_LOG_XDEV_IFL_T_HEX(xdev, cond_level, data, data_size)                                                        \
	U_LOG_XDEV_IFL_HEX(U_LOGGING_TRACE, cond_level, xdev, data, data_size)
//! Conditionally log a device-related memory hexdump message at U_LOGGING_DEBUG level.
#define U_LOG_XDEV_IFL_D_HEX(xdev, cond_level, data, data_size)                                                        \
	U_LOG_XDEV_IFL_HEX(U_LOGGING_DEBUG, cond_level, xdev, data, data_size)
/*!
 * @}
 */

/*!
 * @name Device-related logging macros that always log.
 *
 * These wrap U_LOG_XDEV() to supply the @p level - which is only used for formatting the output, these macros always
 * log regardless of level.
 *
 * @param xdev The @ref xrt_device pointer associated with this message.
 * @param ... Format string and optional format arguments.
 * @{
 */
//! Log a device-related message at U_LOGGING_TRACE level (always logs).
#define U_LOG_XDEV_T(xdev, ...) U_LOG_XDEV(U_LOGGING_TRACE, xdev, __VA_ARGS__)
//! Log a device-related message at U_LOGGING_DEBUG level (always logs).
#define U_LOG_XDEV_D(xdev, ...) U_LOG_XDEV(U_LOGGING_DEBUG, xdev, __VA_ARGS__)
//! Log a device-related message at U_LOGGING_INFO level (always logs).
#define U_LOG_XDEV_I(xdev, ...) U_LOG_XDEV(U_LOGGING_INFO, xdev, __VA_ARGS__)
//! Log a device-related message at U_LOGGING_WARN level (always logs).
#define U_LOG_XDEV_W(xdev, ...) U_LOG_XDEV(U_LOGGING_WARN, xdev, __VA_ARGS__)
//! Log a device-related message at U_LOGGING_ERROR level (always logs).
#define U_LOG_XDEV_E(xdev, ...) U_LOG_XDEV(U_LOGGING_ERROR, xdev, __VA_ARGS__)
/*!
 * @}
 */

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
