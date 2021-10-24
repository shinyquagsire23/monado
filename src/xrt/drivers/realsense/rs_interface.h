// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to RealSense devices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_rs
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct xrt_frame_context;

/*!
 * @defgroup drv_rs Intel RealSense driver
 * @ingroup drv
 *
 * @brief Driver for the SLAM-capable Intel Realsense devices.
 */

#define RS_TRACKING_DISABLED -1
#define RS_TRACKING_UNSPECIFIED 0
#define RS_TRACKING_DEVICE_SLAM 1
#define RS_TRACKING_HOST_SLAM 2

/*!
 * Create a auto prober for rs devices.
 *
 * @ingroup drv_rs
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
 * @dir drivers/realsense
 *
 * @brief @ref drv_rs files.
 */


#ifdef __cplusplus
}
#endif
