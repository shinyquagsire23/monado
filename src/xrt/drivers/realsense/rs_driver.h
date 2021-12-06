// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal header for the RealSense driver.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup drv_realsense
 */
#pragma once

#include "xrt/xrt_prober.h"

#include "rs_interface.h"

#include <librealsense2/rs.h>
#include <librealsense2/h/rs_pipeline.h>

/*!
 * @addtogroup drv_realsense
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

//! Container to store and manage useful objects from the RealSense API
struct rs_container
{
	rs2_error *error_status;

	// Used by prober and devices
	rs2_context *context;         //!< RealSense API context
	rs2_device_list *device_list; //!< List of connected RealSense devices
	int device_count;             //!< Length of device_list

	// Used by devices
	int device_idx;                //!< `device` index in `device_list`
	rs2_device *device;            //!< Main device
	rs2_pipeline *pipeline;        //!< RealSense running pipeline
	rs2_config *config;            //!< Pipeline streaming configuration
	rs2_pipeline_profile *profile; //!< Pipeline profile
};

//! Cleans up an @ref rs_container and resets its fields to NULL;
static void
rs_container_cleanup(struct rs_container *rsc)
{
	// A note about what is and what is not being deleted:
	// In its documentation, the RealSense API specifies which calls require the
	// caller to delete the returned object afterwards. By looking at the code of
	// the API it seems that when that is not explicitly pointed out in the
	// interface documentation, you should *not* delete the returned object.

	// clang-format off
	if (rsc->profile) rs2_delete_pipeline_profile(rsc->profile);
	if (rsc->config) rs2_delete_config(rsc->config);
	if (rsc->pipeline) rs2_delete_pipeline(rsc->pipeline);
	if (rsc->device) rs2_delete_device(rsc->device);
	if (rsc->device_list) rs2_delete_device_list(rsc->device_list);
	if (rsc->context) rs2_delete_context(rsc->context);
	if (rsc->error_status) rs2_free_error(rsc->error_status);
	// clang-format on

	rsc->profile = NULL;
	rsc->config = NULL;
	rsc->pipeline = NULL;
	rsc->device = NULL;
	rsc->device_idx = -1;
	rsc->device_count = 0;
	rsc->device_list = NULL;
	rsc->context = NULL;
	rsc->error_status = NULL;
}

//! Create a RealSense device tracked with device-SLAM (T26x).
struct xrt_device *
rs_ddev_create(int device_idx);

//! Create RealSense device tracked with host-SLAM (one with camera and IMU streams)
struct xrt_device *
rs_hdev_create(struct xrt_prober *xp, int device_idx);

/*!
 * @}
 */

#ifdef __cplusplus
}
#endif
