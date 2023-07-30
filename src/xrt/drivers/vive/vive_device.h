// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  vive device header
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include "xrt/xrt_device.h"
#include "os/os_threading.h"
#include "util/u_logging.h"
#include "util/u_debug.h"
#include "util/u_time.h"
#include "util/u_var.h"
#include "math/m_imu_3dof.h"
#include "math/m_relation_history.h"

#include "vive/vive_config.h"

#include "vive_lighthouse.h"
#include "xrt/xrt_tracking.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @implements xrt_device
 */
struct vive_device
{
	struct xrt_device base;
	struct os_hid_device *mainboard_dev;
	struct os_hid_device *sensors_dev;
	struct os_hid_device *watchman_dev;

	struct lighthouse_watchman watchman;

	struct os_thread_helper sensors_thread;
	struct os_thread_helper watchman_thread;
	struct os_thread_helper mainboard_thread;

	struct
	{
		timepoint_ns last_sample_ts_ns;
		uint32_t last_sample_ticks;
		uint8_t sequence;
	} imu;

	struct
	{
		uint16_t ipd;
		uint16_t lens_separation;
		uint16_t proximity;
		uint8_t button;
		uint8_t audio_button;
	} board;

	enum u_logging_level log_level;
	bool disconnect_notified;

	struct
	{
		struct u_var_button switch_tracker_btn;
		char hand_status[128];
		char slam_status[128];
	} gui;

	struct vive_config config;

	struct
	{
		//! Protects all members of the `fusion` substruct.
		struct os_mutex mutex;

		//! Main fusion calculator.
		struct m_imu_3dof i3dof;

		//! Prediction
		struct m_relation_history *relation_hist;
	} fusion;

	//! Fields related to camera-based tracking (SLAM and hand tracking)
	struct
	{
		//! SLAM tracker.
		struct xrt_tracked_slam *slam;

		//! Set at start. Whether the SLAM tracker was initialized.
		bool slam_enabled;

		//! Set at start. Whether the hand tracker was initialized.
		bool hand_enabled;

		//! SLAM systems track the IMU pose, enabling this corrects it to middle of the eyes
		bool imu2me;
	} tracking;

	/*!
	 * Offset for tracked pose offsets (applies to both fusion and SLAM).
	 * Applied when getting the tracked poses, so is effectivily a offset
	 * to increase or decrease prediction.
	 */
	struct u_var_draggable_f32 tracked_offset_ms;

	struct xrt_pose P_imu_me; //!< IMU to head/display/middle-of-eyes transform in OpenXR coords

	//! Whether to track the HMD with 6dof SLAM or fallback to the 3dof tracker
	bool slam_over_3dof;

	//! In charge of managing raw samples, redirects them for tracking
	struct vive_source *source;

	//! Last tracked pose
	struct xrt_pose pose;

	//! Additional offset to apply to `pose`
	struct xrt_pose offset;
};


/*!
 * Summary of the status of various trackers.
 *
 * @todo Creation flow is a bit broken for now, in the future this info should be closer
 * to the tracker creation code, thus avoiding the need to pass it around like this.
 */
struct vive_tracking_status
{
	bool slam_wanted;
	bool slam_supported;
	bool slam_enabled;

	//! Has Monado been built with the correct libraries to do optical hand tracking?
	bool hand_supported;

	//! Did we find controllers?
	bool controllers_found;

	//! If this is set to ON, we always do optical hand tracking even if controllers were found.
	//! If this is set to AUTO, we do optical hand tracking only if no controllers were found.
	//! If this is set to OFF, we don't do optical hand tracking.
	enum debug_tristate_option hand_wanted;

	//! Computed in target_builder_lighthouse.c based on the past three
	bool hand_enabled;
};

void
vive_set_trackers_status(struct vive_device *d, struct vive_tracking_status status);

struct vive_device *
vive_device_create(struct os_hid_device *mainboard_dev,
                   struct os_hid_device *sensors_dev,
                   struct os_hid_device *watchman_dev,
                   enum VIVE_VARIANT variant,
                   struct vive_tracking_status tstatus,
                   struct vive_source *vs);

#ifdef __cplusplus
}
#endif
