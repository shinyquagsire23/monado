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
#include "xrt/xrt_config_drivers.h"

#include "tracking/t_tracking.h"
#include "tracking/t_hand_tracking.h"
#include "xrt/xrt_prober.h"


#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_ht Camera based hand tracking
 * @ingroup drv
 *
 * @brief Camera based hand tracking
 */

/*!
 * Create hand tracker.
 *
 * @ingroup drv_ht
 *
 * @param xfctx Frame context to attach the tracker to
 * @param calib Calibration struct for stereo camera
 * @param out_sinks Sinks to stream camera data to
 * @param out_device Newly created hand tracker "device"
 * @return int 0 on success
 */
int
ht_device_create(struct xrt_frame_context *xfctx,
                 struct t_stereo_camera_calibration *calib,
                 struct t_camera_extra_info extra_camera_info,
                 struct xrt_slam_sinks **out_sinks,
                 struct xrt_device **out_device);


/*!
 * @dir drivers/ht
 *
 * @brief @ref drv_ht files.
 */
#ifdef __cplusplus
}
#endif
