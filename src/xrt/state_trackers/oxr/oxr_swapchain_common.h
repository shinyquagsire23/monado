// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helper functions for @ref oxr_swapchain functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#pragma once

#include "oxr_objects.h"


/*
 *
 * Helper defines.
 *
 */

#define CHECK_OXR_RET(THING)                                                                                           \
	do {                                                                                                           \
		XrResult check_ret = (THING);                                                                          \
		if (check_ret != XR_SUCCESS) {                                                                         \
			return check_ret;                                                                              \
		}                                                                                                      \
	} while (false)


/*
 *
 * Verify functions.
 *
 */

static inline XrResult
oxr_swapchain_verify_wait_state(struct oxr_logger *log, struct oxr_swapchain *sc)
{
	if (sc->inflight.yes) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID, "Swapchain has already been waited, call release");
	}

	if (u_index_fifo_is_empty(&sc->acquired.fifo)) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID, "No image acquired");
	}

	return XR_SUCCESS;
}


/*
 *
 * Common shared functions.
 *
 */

/*!
 * The shared code of the acquire call used by all graphics APIs.
 *
 * @param      log       Logger set with the current OpenXR function call context.
 * @param      sc        Swapchain.
 * @param[out] out_index Return of the acquired index.
 */
XrResult
oxr_swapchain_common_acquire(struct oxr_logger *log, struct oxr_swapchain *sc, uint32_t *out_index);

/*!
 * The shared code of the wait call used by all graphics APIs.
 *
 * @param log     Logger set with the current OpenXR function call context.
 * @param sc      Swapchain.
 * @param timeout Return of the acquired index.
 */
XrResult
oxr_swapchain_common_wait(struct oxr_logger *log, struct oxr_swapchain *sc, XrDuration timeout);

/*!
 * The shared code of the release call used by all graphics APIs.
 *
 * @param log   Logger set with the current OpenXR function call context.
 * @param sc    Swapchain.
 */
XrResult
oxr_swapchain_common_release(struct oxr_logger *log, struct oxr_swapchain *sc);

/*!
 * Shared create function for swapchains, called by grahpics API specific
 * implementations list below. Does most init, but not @ref xrt_swapchain
 * allocation and other API specific things.
 *
 * @param      log           Logger set with the current OpenXR function call context.
 * @param      sess          OpenXR session
 * @param      createInfo    Creation info.
 * @param      sc            Swapchain.
 * @param[out] out_swapchain Return of the allocated swapchain.
 */
XrResult
oxr_swapchain_common_create(struct oxr_logger *log,
                            struct oxr_session *sess,
                            const XrSwapchainCreateInfo *createInfo,
                            struct oxr_swapchain **out_swapchain);
