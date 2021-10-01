// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to camera based hand tracking driver code.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

#include "xrt/xrt_device.h"

#include "tracking/t_tracking.h"


#ifdef __cplusplus
extern "C" {
#endif

enum ht_run_type
{
	HT_RUN_TYPE_VALVE_INDEX,
	HT_RUN_TYPE_NORTH_STAR,
};
// YES this is stupid. PLEASE bikeshed me on this when the time comes, this is terrible.

// With Valve Index, we use the frameserver prober and look for the Valve Index camera, and we give the joint poses out
// in the space of the left (unrectified) camera.

// With North Star, (really just Moses's headset :)) we hard-code to opening up a depthai_fs_stereo_rgb and give the
// joint poses out in the space of the "center" of the stereo camera. (Why? Because I don't have exact extrinsics from
// the NS "eyes" to the cameras. Less code this way.)

/*!
 * @defgroup drv_ht Camera based hand tracking
 * @ingroup drv
 *
 * @brief
 */

/*!
 * Create a hand tracker device.
 *
 * @ingroup drv_ht
 */
struct xrt_device *
ht_device_create(struct xrt_prober *xp, struct t_stereo_camera_calibration *calib);

/*!
 * @dir drivers/ht
 *
 * @brief @ref drv_ht files.
 */
#ifdef __cplusplus
}
#endif
