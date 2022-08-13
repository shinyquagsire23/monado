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
 * Create a hand tracker device.
 *
 * @ingroup drv_ht
 */
struct xrt_device *
ht_device_create_index(struct xrt_prober *xp, struct t_stereo_camera_calibration *calib);

/*!
 * Create hand tracker for WMR devices.
 *
 * @note The frame context comes from the WMR device.
 *
 * @ingroup drv_ht
 *
 * @param xfctx WMR context to attach the tracker to
 * @param calib Calibration struct for stereo camera
 * @param algorithm_choice Which algorithm to use for hand tracking
 * @param out_sinks Sinks to stream camera data to
 * @param out_device Newly created hand tracker "device"
 * @return int 0 on success
 */
int
ht_device_create(struct xrt_frame_context *xfctx,
                 struct t_stereo_camera_calibration *calib,
                 enum t_hand_tracking_algorithm algorithm_choice,
                 struct t_camera_extra_info extra_camera_info,
                 struct xrt_slam_sinks **out_sinks,
                 struct xrt_device **out_device);

#ifdef XRT_BUILD_DRIVER_DEPTHAI
struct xrt_device *
ht_device_create_depthai_ov9282(void);

struct xrt_auto_prober *
ht_create_auto_prober();
#endif

/*!
 * @dir drivers/ht
 *
 * @brief @ref drv_ht files.
 */
#ifdef __cplusplus
}
#endif
