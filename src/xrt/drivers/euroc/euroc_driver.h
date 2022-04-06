// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Internal header for the euroc driver utilities.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup drv_euroc
 */
#pragma once

#include "util/u_logging.h"
#include "euroc_interface.h"
#include <assert.h>

/*!
 * @addtogroup drv_euroc
 * @{
 */

#define EUROC_TRACE(e, ...) U_LOG_IFL_T(e->log_level, __VA_ARGS__)
#define EUROC_DEBUG(e, ...) U_LOG_IFL_D(e->log_level, __VA_ARGS__)
#define EUROC_INFO(e, ...) U_LOG_IFL_I(e->log_level, __VA_ARGS__)
#define EUROC_WARN(e, ...) U_LOG_IFL_W(e->log_level, __VA_ARGS__)
#define EUROC_ERROR(e, ...) U_LOG_IFL_E(e->log_level, __VA_ARGS__)
#define EUROC_ASSERT(predicate, ...)                                                                                   \
	do {                                                                                                           \
		bool p = predicate;                                                                                    \
		if (!p) {                                                                                              \
			U_LOG(U_LOGGING_ERROR, __VA_ARGS__);                                                           \
			assert(false && "EUROC_ASSERT failed: " #predicate);                                           \
			exit(EXIT_FAILURE);                                                                            \
		}                                                                                                      \
	} while (false);
#define EUROC_ASSERT_(predicate) EUROC_ASSERT(predicate, "Assertion failed " #predicate)

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @}
 */

#ifdef __cplusplus
}
#endif
