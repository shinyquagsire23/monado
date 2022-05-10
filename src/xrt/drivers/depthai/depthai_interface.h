// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface header for DepthAI camera.
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_depthai
 */

#pragma once

#include "xrt/xrt_frameserver.h"

#ifdef __cplusplus
extern "C" {
#endif

#undef DEPTHAI_HAS_MULTICAM_SUPPORT


struct t_stereo_camera_calibration;


/*!
 * @defgroup drv_depthai DepthAI frameserver driver
 * @ingroup drv
 *
 * @brief Frameserver for the DepthAI camera module.
 */

/*!
 * Create a DepthAI frameserver using a single RGB camera.
 *
 * @ingroup drv_depthai
 */
struct xrt_fs *
depthai_fs_monocular_rgb(struct xrt_frame_context *xfctx);

/*!
 * Create a DepthAI frameserver using two gray cameras.
 * Either OAK-D or OAK-D Lite. Custom FFC setups may or may not work.
 *
 * @ingroup drv_depthai
 */
struct xrt_fs *
depthai_fs_stereo_grayscale(struct xrt_frame_context *xfctx);

/*!
 * Create a DepthAI frameserver using two gray cameras and the IMU.
 * Only OAK-D - OAK-D Lite doesn't have an IMU. Custom FFC setups may or may not work.
 *
 * @ingroup drv_depthai
 */
struct xrt_fs *
depthai_fs_stereo_grayscale_and_imu(struct xrt_frame_context *xfctx);

/*!
 * Create a DepthAI frameserver using two gray cameras.
 * Any DepthAI device with an IMU.
 *
 * @ingroup drv_depthai
 */
struct xrt_fs *
depthai_fs_just_imu(struct xrt_frame_context *xfctx);

#ifdef DEPTHAI_HAS_MULTICAM_SUPPORT
/*!
 * Create a DepthAI frameserver using two rgb cameras.
 *
 * @ingroup drv_depthai
 */
struct xrt_fs *
depthai_fs_stereo_rgb(struct xrt_frame_context *xfctx);
#endif

/*!
 * Get the stereo calibration from a depthAI frameserver.
 *
 * @ingroup drv_depthai
 */
bool
depthai_fs_get_stereo_calibration(struct xrt_fs *xfs, struct t_stereo_camera_calibration **c_ptr);


#ifdef __cplusplus
}
#endif
