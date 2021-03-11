// Copyright 2021, Mateo de Mayo.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Internal header for qwerty_device and its friends.
 * @author Mateo de Mayo <mateodemayo@gmail.com>
 * @ingroup drv_qwerty
 */
#pragma once

#include "xrt/xrt_device.h"

#define QWERTY_HMD_STR "Qwerty HMD"
#define QWERTY_HMD_TRACKER_STR QWERTY_HMD_STR " Tracker"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @addtogroup drv_qwerty
 * @{
 */

//! @implements xrt_device
struct qwerty_device
{
	struct xrt_device base;
};

//! Cast to qwerty_device. Ensures returning a valid device or crashing.
struct qwerty_device *
qwerty_device(struct xrt_device *xd);

//! Create qwerty_hmd. Crash on failure.
struct qwerty_device *
qwerty_hmd_create(void);

/*!
 * @}
 */

#ifdef __cplusplus
}
#endif
