// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to RealSense devices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_realsense
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct xrt_frame_context;

/*!
 * @defgroup drv_realsense Intel RealSense driver
 * @ingroup drv
 *
 * @brief Driver for the SLAM-capable Intel Realsense devices.
 */

#define REALSENSE_MOVIDIUS_VID 0x03E7
#define REALSENSE_MOVIDIUS_PID 0x2150

#define REALSENSE_TM2_VID 0x8087
#define REALSENSE_TM2_PID 0x0B37

#define RS_TRACKING_DISABLED -1
#define RS_TRACKING_UNSPECIFIED 0
#define RS_TRACKING_DEVICE_SLAM 1
#define RS_TRACKING_HOST_SLAM 2

/*!
 * Create a auto prober for rs devices.
 *
 * @ingroup drv_realsense
 */
struct xrt_auto_prober *
rs_create_auto_prober(void);

/*!
 * Creates a RealSense SLAM source from the appropriate @p device_idx.
 * The streaming configuration is loaded from the global config file.
 *
 * @param xfctx Frame context this frameserver lifetime is tied to.
 * @param device_idx Index of the realsense device to use. Usually 0 if you only
 * have one RealSense device.
 * @return Frameserver with SLAM streaming capabilities.
 */
struct xrt_fs *
rs_source_create(struct xrt_frame_context *xfctx, int device_idx);

/*!
 * Creates an xrt_device that exposes the onboard tracking of a Realsense device
 * (ie. probably a T265)
 * @return An xrt_device that you can call get_tracked_pose on with XRT_INPUT_GENERIC_TRACKER_POSE
 */
struct xrt_device *
rs_create_tracked_device_internal_slam(void);

/*!
 * @dir drivers/realsense
 *
 * @brief @ref drv_realsense files.
 */


#ifdef __cplusplus
}
#endif
