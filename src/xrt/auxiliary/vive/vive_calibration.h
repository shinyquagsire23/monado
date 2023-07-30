// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vive calibration getters.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_vive
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "tracking/t_tracking.h"


#ifdef __cplusplus
extern "C" {
#endif


struct vive_config;

/*!
 * Get a @ref t_stereo_camera_calibration and @ref xrt_pose from left camera to head translation.
 *
 * @ingroup aux_vive
 */
bool
vive_get_stereo_camera_calibration(const struct vive_config *d,
                                   struct t_stereo_camera_calibration **calibration_ptr_to_ref,
                                   struct xrt_pose *out_head_in_left_camera);

/*!
 * Get a @ref t_slam_camera_calibration one for each camera.
 *
 * @ingroup aux_vive
 */
void
vive_get_slam_cams_calib(const struct vive_config *d,
                         struct t_slam_camera_calibration *out_calib0,
                         struct t_slam_camera_calibration *out_calib1);

/*!
 * Get a @ref t_imu_calibration for the IMU.
 *
 * @ingroup aux_vive
 */
void
vive_get_imu_calibration(const struct vive_config *d, struct t_imu_calibration *out_calib);

/*!
 * Get a @ref t_slam_imu_calibration for the IMU.
 *
 * @ingroup aux_vive
 */
void
vive_get_slam_imu_calibration(const struct vive_config *d, struct t_slam_imu_calibration *out_calib);


#ifdef __cplusplus
} // extern "C"
#endif
