// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining the tracking system integration in Monado.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#define XRT_TRACKING_NAME_LEN 256

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

struct time_state;

enum xrt_tracking_type
{
	// The device(s) are never tracked.
	XRT_TRACKING_TYPE_NONE,
};

/*!
 * A tracking system or device origin.
 *
 * @ingroup xrt_iface
 */
struct xrt_tracking
{
	//! For debugging.
	char name[XRT_TRACKING_NAME_LEN];

	//! What can the state tracker expect from this tracking system.
	enum xrt_tracking_type type;

	/*!
	 * Read and written to by the state-tracker using the device(s)
	 * this tracking system is tracking.
	 */
	struct xrt_pose offset;
};


#ifdef __cplusplus
}
#endif
