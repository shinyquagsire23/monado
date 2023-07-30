// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  File holding helper for @ref xrt_result_t results.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#pragma once

#include "xrt/xrt_results.h"
#include "oxr_objects.h"


/*!
 * Helper define to check results from 'xrt_` functions (@ref xrt_result_t) and
 * also set any needed state.
 *
 * @ingroup oxr_main
 */
#define OXR_CHECK_XRET(LOG, SESS, RESULTS, FUNCTION)                                                                   \
	do {                                                                                                           \
		xrt_result_t check_ret = (RESULTS);                                                                    \
		if (check_ret == XRT_ERROR_IPC_FAILURE) {                                                              \
			(SESS)->has_lost = true;                                                                       \
			return oxr_error(log, XR_ERROR_INSTANCE_LOST, "Call to " #FUNCTION " failed");                 \
		}                                                                                                      \
		if (check_ret != XRT_SUCCESS) {                                                                        \
			return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Call to " #FUNCTION " failed");               \
		}                                                                                                      \
	} while (false)
