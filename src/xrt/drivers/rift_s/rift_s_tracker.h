/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 */

/*!
 * @file
 * @brief  HMD tracker handling
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */

#pragma once

#include "math/m_imu_3dof.h"
#include "os/os_threading.h"
#include "util/u_var.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "tracking/t_tracking.h"

#include "rift_s_firmware.h"

/* Oculus Rift S HMD Tracking */
#ifndef RIFT_S_TRACKER_H
#define RIFT_S_TRACKER_H

struct rift_s_hmd_config;

enum rift_s_tracker_pose
{
	RIFT_S_TRACKER_POSE_IMU,
	RIFT_S_TRACKER_POSE_LEFT_CAMERA,
	RIFT_S_TRACKER_POSE_DEVICE,
};

struct rift_s_tracker
{
	struct xrt_device base;

	//! Protects shared access to 3dof and pose storage
	struct os_mutex mutex;

	//! Don't process IMU / video until started
	bool ready_for_data;

	struct
	{
		//! Main fusion calculator.
		struct m_imu_3dof i3dof;

		//! The last angular velocity from the IMU, for prediction.
		struct xrt_vec3 last_angular_velocity;

		//! When did we get the last IMU sample, device clock
		uint64_t last_imu_timestamp_ns;

		//! Last IMU sample local system clock
		timepoint_ns last_imu_local_timestamp_ns;
	} fusion;

	//! Fields related to camera-based tracking (SLAM and hand tracking)
	struct
	{
		//! SLAM tracker.
		//! @todo Right now, we are not consistent in how we interface with
		//! trackers. In particular, we have a @ref xrt_tracked_slam field but not
		//! an equivalent for hand tracking.
		struct xrt_tracked_slam *slam;

		//! Set at start. Whether the SLAM tracker was initialized.
		bool slam_enabled;

		//! Set at start. Whether the hand tracker was initialized.
		bool hand_enabled;
	} tracking;

	// Correction offset poses from firmware
	struct xrt_pose device_from_imu;
	struct xrt_pose left_cam_from_imu;

	//!< Estimated offset from HMD device timestamp to local monotonic clock
	uint64_t seen_clock_observations;
	bool have_hw2mono;
	time_duration_ns hw2mono;
	timepoint_ns last_frame_time;

	//! Adjustment to apply to camera timestamps to bring them into the
	// same 32-bit range as the IMU times
	int64_t camera_ts_offset;

	//! Whether to track the HMD with 6dof SLAM or fallback to the `fusion` 3dof tracker
	bool slam_over_3dof;

	//! Last tracked pose
	struct xrt_pose pose;

	/* Stereo calibration for the front 2 cameras */
	struct t_stereo_camera_calibration *stereo_calib;
	struct t_slam_calibration slam_calib;

	/* Input sinks that the camera delivers SLAM frames to */
	struct xrt_slam_sinks in_slam_sinks;

	/* SLAM/HT sinks we deliver imu and frame data to */
	struct xrt_slam_sinks slam_sinks;

	struct xrt_device *handtracker;

	struct
	{
		struct u_var_button hmd_screen_enable_btn;
		struct u_var_button switch_tracker_btn;
		char hand_status[128];
		char slam_status[128];
	} gui;
};

struct rift_s_tracker *
rift_s_tracker_create(struct xrt_tracking_origin *origin,
                      struct xrt_frame_context *xfctx,
                      struct rift_s_hmd_config *hmd_config);
void
rift_s_tracker_start(struct rift_s_tracker *t);
void
rift_s_tracker_destroy(struct rift_s_tracker *t);
void
rift_s_tracker_add_debug_ui(struct rift_s_tracker *t, void *root);

struct xrt_slam_sinks *
rift_s_tracker_get_slam_sinks(struct rift_s_tracker *t);
struct xrt_device *
rift_s_tracker_get_hand_tracking_device(struct rift_s_tracker *t);

void
rift_s_tracker_clock_update(struct rift_s_tracker *t, uint64_t device_timestamp_ns, timepoint_ns local_timestamp_ns);

void
rift_s_tracker_imu_update(struct rift_s_tracker *t,
                          uint64_t device_timestamp_ns,
                          const struct xrt_vec3 *accel,
                          const struct xrt_vec3 *gyro);

void
rift_s_tracker_push_slam_frames(struct rift_s_tracker *t,
                                uint64_t frame_ts_ns,
                                struct xrt_frame *frames[RIFT_S_CAMERA_COUNT]);
void
rift_s_tracker_get_tracked_pose(struct rift_s_tracker *t,
                                enum rift_s_tracker_pose pose,
                                uint64_t at_timestamp_ns,
                                struct xrt_space_relation *out_relation);

#endif
