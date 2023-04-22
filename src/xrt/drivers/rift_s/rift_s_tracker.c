/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * Copyright 2023, Collabora, Ltd.
 * SPDX-License-Identifier: BSL-1.0
 *
 */
/*!
 * @file
 * @brief  Driver code for Oculus Rift S headsets
 *
 * Implementation for the HMD 3dof and 6dof tracking
 *
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <inttypes.h>

#include "math/m_api.h"
#include "math/m_clock_offset.h"
#include "math/m_space.h"
#include "math/m_vec3.h"

#include "os/os_time.h"

#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_sink.h"
#include "util/u_trace_marker.h"
#include "util/u_var.h"

#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_device.h"

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
#include "../drivers/ht/ht_interface.h"
#include "../multi_wrapper/multi.h"
#endif

#include "rift_s.h"
#include "rift_s_interface.h"
#include "rift_s_util.h"
#include "rift_s_tracker.h"

#ifdef XRT_FEATURE_SLAM
static const bool slam_supported = true;
#else
static const bool slam_supported = false;
#endif

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
static const bool hand_supported = true;
#else
static const bool hand_supported = false;
#endif

//! Specifies whether the user wants to use a SLAM tracker.
DEBUG_GET_ONCE_BOOL_OPTION(rift_s_slam, "RIFT_S_SLAM", true)

//! Specifies whether the user wants to use the hand tracker.
DEBUG_GET_ONCE_BOOL_OPTION(rift_s_handtracking, "RIFT_S_HANDTRACKING", true)

static void
rift_s_tracker_get_tracked_pose_imu(struct xrt_device *xdev,
                                    enum xrt_input_name name,
                                    uint64_t at_timestamp_ns,
                                    struct xrt_space_relation *out_relation);

static void
rift_s_tracker_switch_method_cb(void *t_ptr)
{
	DRV_TRACE_MARKER();

	struct rift_s_tracker *t = t_ptr;
	t->slam_over_3dof = !t->slam_over_3dof;
	struct u_var_button *btn = &t->gui.switch_tracker_btn;

	if (t->slam_over_3dof) { // Use SLAM
		snprintf(btn->label, sizeof(btn->label), "Switch to 3DoF Tracking");
	} else { // Use 3DoF
		snprintf(btn->label, sizeof(btn->label), "Switch to SLAM Tracking");

		os_mutex_lock(&t->mutex);
		m_imu_3dof_reset(&t->fusion.i3dof);
		t->fusion.i3dof.rot = t->pose.orientation;
		os_mutex_unlock(&t->mutex);
	}
}

XRT_MAYBE_UNUSED void
rift_s_fill_slam_imu_calibration(struct rift_s_tracker *t, struct rift_s_hmd_config *hmd_config)
{
	/* FIXME: Validate these hard coded standard deviations against
	 * some actual at-rest IMU measurements */
	const double a_bias_std = 0.001;
	const double a_noise_std = 0.016;

	const double g_bias_std = 0.0001;
	const double g_noise_std = 0.000282;

	/* we pass already corrected accel and gyro
	 * readings to Basalt, so the transforms and
	 * offsets are just identity / zero matrices */
	struct t_imu_calibration imu_calib = {
	    .accel =
	        {
	            .transform =
	                {
	                    {1.0, 0.0, 0.0},
	                    {0.0, 1.0, 0.0},
	                    {0.0, 0.0, 1.0},
	                },
	            .offset =
	                {
	                    0,
	                },
	            .bias_std = {a_bias_std, a_bias_std, a_bias_std},
	            .noise_std = {a_noise_std, a_noise_std, a_noise_std},
	        },
	    .gyro =
	        {
	            .transform =
	                {
	                    {1.0, 0.0, 0.0},
	                    {0.0, 1.0, 0.0},
	                    {0.0, 0.0, 1.0},
	                },
	            .offset =
	                {
	                    0,
	                },
	            .bias_std = {g_bias_std, g_bias_std, g_bias_std},
	            .noise_std = {g_noise_std, g_noise_std, g_noise_std},
	        },
	};

	struct t_slam_imu_calibration calib = {
	    .base = imu_calib,
	    .frequency = hmd_config->imu_config_info.imu_hz,
	};

	t->slam_calib.imu = calib;
}

//! Extended camera calibration for SLAM
static void
rift_s_fill_slam_cameras_calibration(struct rift_s_tracker *t, struct rift_s_hmd_config *hmd_config)
{
	/* SLAM frames are every 2nd frame of 60Hz camera feed */
	const int CAMERA_FREQUENCY = 30;

	struct rift_s_camera_calibration_block *camera_calibration = &hmd_config->camera_calibration;

	/* Compute the IMU from cam transform for each cam */
	struct xrt_pose device_from_imu, imu_from_device;
	math_pose_from_isometry(&hmd_config->imu_calibration.device_from_imu, &device_from_imu);
	math_pose_invert(&device_from_imu, &imu_from_device);

	t->slam_calib.cam_count = RIFT_S_CAMERA_COUNT;
	for (int i = 0; i < RIFT_S_CAMERA_COUNT; i++) {
		enum rift_s_camera_id cam_id = CAM_IDX_TO_ID[i];
		struct rift_s_camera_calibration *cam = &camera_calibration->cameras[cam_id];

		struct xrt_pose device_from_cam;
		math_pose_from_isometry(&cam->device_from_camera, &device_from_cam);

		struct xrt_pose P_imu_cam;
		math_pose_transform(&imu_from_device, &device_from_cam, &P_imu_cam);

		struct xrt_matrix_4x4 T_imu_cam;
		math_matrix_4x4_isometry_from_pose(&P_imu_cam, &T_imu_cam);

		RIFT_S_DEBUG("IMU cam%d cam pose %f %f %f orient %f %f %f %f", i, P_imu_cam.position.x,
		             P_imu_cam.position.y, P_imu_cam.position.z, P_imu_cam.orientation.x,
		             P_imu_cam.orientation.y, P_imu_cam.orientation.z, P_imu_cam.orientation.w);

		struct t_slam_camera_calibration calib = {
		    .base = rift_s_get_cam_calib(&hmd_config->camera_calibration, cam_id),
		    .frequency = CAMERA_FREQUENCY,
		    .T_imu_cam = T_imu_cam,
		};
		t->slam_calib.cams[i] = calib;
	}
}

static void
rift_s_fill_slam_calibration(struct rift_s_tracker *t, struct rift_s_hmd_config *hmd_config)
{
	rift_s_fill_slam_imu_calibration(t, hmd_config);
	rift_s_fill_slam_cameras_calibration(t, hmd_config);
}

static struct xrt_slam_sinks *
rift_s_create_slam_tracker(struct rift_s_tracker *t, struct xrt_frame_context *xfctx)
{
	DRV_TRACE_MARKER();

	struct xrt_slam_sinks *sinks = NULL;

#ifdef XRT_FEATURE_SLAM
	struct t_slam_tracker_config config = {0};
	t_slam_fill_default_config(&config);

	/* No need to refcount these parameters */
	config.cam_count = RIFT_S_CAMERA_COUNT;
	config.slam_calib = &t->slam_calib;

	int create_status = t_slam_create(xfctx, &config, &t->tracking.slam, &sinks);
	if (create_status != 0) {
		return NULL;
	}

	int start_status = t_slam_start(t->tracking.slam);
	if (start_status != 0) {
		return NULL;
	}

	RIFT_S_DEBUG("Rift S SLAM tracker successfully started");
#endif

	return sinks;
}

static int
rift_s_create_hand_tracker(struct rift_s_tracker *t,
                           struct xrt_frame_context *xfctx,
                           struct xrt_slam_sinks **out_sinks,
                           struct xrt_device **out_device)
{
	DRV_TRACE_MARKER();

	struct xrt_slam_sinks *sinks = NULL;
	struct xrt_device *device = NULL;

#ifdef XRT_BUILD_DRIVER_HANDTRACKING

	//!@todo What's a sensible boundary for Rift S?
	struct t_camera_extra_info extra_camera_info = {
	    0,
	};
	extra_camera_info.views[0].boundary_type = HT_IMAGE_BOUNDARY_NONE;
	extra_camera_info.views[1].boundary_type = HT_IMAGE_BOUNDARY_NONE;

	extra_camera_info.views[0].camera_orientation = CAMERA_ORIENTATION_90;
	extra_camera_info.views[1].camera_orientation = CAMERA_ORIENTATION_90;

	int create_status = ht_device_create(xfctx,           //
	                                     t->stereo_calib, //
	                                     extra_camera_info,
	                                     &sinks, //
	                                     &device);
	if (create_status != 0) {
		return create_status;
	}

	if (device != NULL) {
		// Attach tracking override that links hand pose to the SLAM tracked position
		// The hand poses need to be rotated 90° because of the way we passed
		// the stereo camera configuration to the hand tracker.
		struct xrt_pose left_cam_rotated_from_imu;
		struct xrt_pose cam_rotate = {.orientation = {.x = 1.0, .y = 0.0, .z = 0.0, .w = 0.0},
		                              .position = {0, 0, 0}};
		math_pose_transform(&cam_rotate, &t->left_cam_from_imu, &left_cam_rotated_from_imu);

		device = multi_create_tracking_override(XRT_TRACKING_OVERRIDE_ATTACHED, device, &t->base,
		                                        XRT_INPUT_GENERIC_TRACKER_POSE, &left_cam_rotated_from_imu);
	}

	RIFT_S_DEBUG("Rift S HMD hand tracker successfully created");
#endif

	*out_sinks = sinks;
	*out_device = device;

	return 0;
}

void
rift_s_tracker_add_debug_ui(struct rift_s_tracker *t, void *root)
{
	u_var_add_gui_header(root, NULL, "Tracking");

	if (t->tracking.slam_enabled) {
		t->gui.switch_tracker_btn.cb = rift_s_tracker_switch_method_cb;
		t->gui.switch_tracker_btn.ptr = t;
		u_var_add_button(root, &t->gui.switch_tracker_btn, "Switch to 3DoF Tracking");
	}

	u_var_add_pose(root, &t->pose, "Tracked Pose");

	u_var_add_gui_header(root, NULL, "3DoF Tracking");
	m_imu_3dof_add_vars(&t->fusion.i3dof, root, "");

	u_var_add_gui_header(root, NULL, "SLAM Tracking");
	u_var_add_ro_text(root, t->gui.slam_status, "Tracker status");

	u_var_add_gui_header(root, NULL, "Hand Tracking");
	u_var_add_ro_text(root, t->gui.hand_status, "Tracker status");
}

/*!
 * Procedure to setup trackers: 3dof, SLAM and hand tracking.
 *
 * Determines which trackers to initialize
 *
 * @param xfctx the frame server that will own processing nodes
 * @param hmd_config HMD configuration and firmware info
 *
 * @return initialised tracker on success, NULL if creation fails
 */
struct rift_s_tracker *
rift_s_tracker_create(struct xrt_tracking_origin *origin,
                      struct xrt_frame_context *xfctx,
                      struct rift_s_hmd_config *hmd_config)
{
	struct rift_s_tracker *t = U_DEVICE_ALLOCATE(struct rift_s_tracker, U_DEVICE_ALLOC_TRACKING_NONE, 1, 0);
	if (t == NULL) {
		return NULL;
	}

	t->base.tracking_origin = origin;
	t->base.get_tracked_pose = rift_s_tracker_get_tracked_pose_imu;

	// Pose / state lock
	int ret = os_mutex_init(&t->mutex);
	if (ret != 0) {
		RIFT_S_ERROR("Failed to init mutex!");
		rift_s_tracker_destroy(t);
		return NULL;
	}

	// Compute IMU and camera device poses for get_tracked_pose relations
	math_pose_from_isometry(&hmd_config->imu_calibration.device_from_imu, &t->device_from_imu);

	struct xrt_pose device_from_left_cam;
	struct rift_s_camera_calibration *left_cam = &hmd_config->camera_calibration.cameras[RIFT_S_CAMERA_FRONT_LEFT];
	math_pose_from_isometry(&left_cam->device_from_camera, &device_from_left_cam);

	struct xrt_pose left_cam_from_device;
	math_pose_invert(&device_from_left_cam, &left_cam_from_device);
	math_pose_transform(&left_cam_from_device, &t->device_from_imu, &t->left_cam_from_imu);

	// Decide whether to initialize the SLAM tracker
	bool slam_wanted = debug_get_bool_option_rift_s_slam();
	bool slam_enabled = slam_supported && slam_wanted;

	// Decide whether to initialize the hand tracker
	bool hand_wanted = debug_get_bool_option_rift_s_handtracking();
	bool hand_enabled = hand_supported && hand_wanted;

	t->tracking.slam_enabled = slam_enabled;
	t->tracking.hand_enabled = hand_enabled;

	t->slam_over_3dof = slam_enabled; // We prefer SLAM over 3dof tracking if possible

	const char *slam_status = t->tracking.slam_enabled ? "Enabled"
	                          : !slam_wanted           ? "Disabled by the user (envvar set to false)"
	                          : !slam_supported        ? "Unavailable (not built)"
	                                                   : NULL;

	const char *hand_status = t->tracking.hand_enabled ? "Enabled"
	                          : !hand_wanted           ? "Disabled by the user (envvar set to false)"
	                          : !hand_supported        ? "Unavailable (not built)"
	                                                   : NULL;

	assert(slam_status != NULL && hand_status != NULL);

	(void)snprintf(t->gui.slam_status, sizeof(t->gui.slam_status), "%s", slam_status);
	(void)snprintf(t->gui.hand_status, sizeof(t->gui.hand_status), "%s", hand_status);

	// Initialize 3DoF tracker
	m_imu_3dof_init(&t->fusion.i3dof, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	t->pose.orientation.w = 1.0f; // All other values set to zero by U_DEVICE_ALLOCATE (which calls U_CALLOC)

	// Construct the stereo camera calibration for the front cameras
	t->stereo_calib = rift_s_create_stereo_camera_calib_rotated(&hmd_config->camera_calibration);
	rift_s_fill_slam_calibration(t, hmd_config);

	// Initialize the input sinks for the camera to send to

	// Initialize SLAM tracker
	struct xrt_slam_sinks *slam_sinks = NULL;
	if (t->tracking.slam_enabled) {
		slam_sinks = rift_s_create_slam_tracker(t, xfctx);
		if (slam_sinks == NULL) {
			RIFT_S_WARN("Unable to setup the SLAM tracker");
			rift_s_tracker_destroy(t);
			return NULL;
		}
	}

	// Initialize hand tracker
	struct xrt_slam_sinks *hand_sinks = NULL;
	struct xrt_device *hand_device = NULL;
	if (t->tracking.hand_enabled) {
		int hand_status = rift_s_create_hand_tracker(t, xfctx, &hand_sinks, &hand_device);
		if (hand_status != 0 || hand_sinks == NULL || hand_device == NULL) {
			RIFT_S_WARN("Unable to setup the hand tracker");
			rift_s_tracker_destroy(t);
			return NULL;
		}
	}

	// Setup sinks depending on tracking configuration
	struct xrt_slam_sinks entry_sinks = {0};
	if (slam_enabled && hand_enabled) {
		struct xrt_frame_sink *entry_cam0_sink = NULL;
		struct xrt_frame_sink *entry_cam1_sink = NULL;

		u_sink_split_create(xfctx, slam_sinks->cams[0], hand_sinks->cams[0], &entry_cam0_sink);
		u_sink_split_create(xfctx, slam_sinks->cams[1], hand_sinks->cams[1], &entry_cam1_sink);

		entry_sinks = *slam_sinks;
		entry_sinks.cams[0] = entry_cam0_sink;
		entry_sinks.cams[1] = entry_cam1_sink;
	} else if (slam_enabled) {
		entry_sinks = *slam_sinks;
	} else if (hand_enabled) {
		entry_sinks = *hand_sinks;
	} else {
		entry_sinks = (struct xrt_slam_sinks){0};
	}

	t->slam_sinks = entry_sinks;
	t->handtracker = hand_device;

	return t;
}

void
rift_s_tracker_destroy(struct rift_s_tracker *t)
{
	t_stereo_camera_calibration_reference(&t->stereo_calib, NULL);

	m_imu_3dof_close(&t->fusion.i3dof);
	os_mutex_destroy(&t->mutex);
}

struct xrt_slam_sinks *
rift_s_tracker_get_slam_sinks(struct rift_s_tracker *t)
{
	return &t->in_slam_sinks;
}

struct xrt_device *
rift_s_tracker_get_hand_tracking_device(struct rift_s_tracker *t)
{
	return t->handtracker;
}

void
rift_s_tracker_clock_update(struct rift_s_tracker *t, uint64_t device_timestamp_ns, timepoint_ns local_timestamp_ns)
{
	os_mutex_lock(&t->mutex);
	time_duration_ns last_hw2mono = t->hw2mono;
	const float freq = 250.0;

	t->seen_clock_observations++;
	if (t->seen_clock_observations < 100)
		goto done;

	m_clock_offset_a2b(freq, device_timestamp_ns, local_timestamp_ns, &t->hw2mono);

	if (!t->have_hw2mono) {
		time_duration_ns change_ns = last_hw2mono - t->hw2mono;
		if (change_ns >= -U_TIME_HALF_MS_IN_NS && change_ns <= U_TIME_HALF_MS_IN_NS) {
			RIFT_S_INFO("HMD device to local clock map stabilised");
			t->have_hw2mono = true;
		}
	}
done:
	os_mutex_unlock(&t->mutex);
}

//! Camera specific logic for clock conversion
static void
clock_hw2mono_get(struct rift_s_tracker *t, uint64_t device_ts, timepoint_ns *out)
{
	*out = t->hw2mono + device_ts;
}

void
rift_s_tracker_imu_update(struct rift_s_tracker *t,
                          uint64_t device_timestamp_ns,
                          const struct xrt_vec3 *accel,
                          const struct xrt_vec3 *gyro)
{
	os_mutex_lock(&t->mutex);

	/* Ignore packets before we're ready and clock is stable */
	if (!t->ready_for_data || !t->have_hw2mono) {
		os_mutex_unlock(&t->mutex);
		return;
	}

	/* Get the smoothed monotonic time estimate for this IMU sample */
	timepoint_ns local_timestamp_ns;

	clock_hw2mono_get(t, device_timestamp_ns, &local_timestamp_ns);

	if (t->fusion.last_imu_local_timestamp_ns != 0 && local_timestamp_ns < t->fusion.last_imu_local_timestamp_ns) {
		RIFT_S_WARN("IMU time went backward by %" PRId64 " ns",
		            local_timestamp_ns - t->fusion.last_imu_local_timestamp_ns);
	} else {
		m_imu_3dof_update(&t->fusion.i3dof, local_timestamp_ns, accel, gyro);
	}

	RIFT_S_TRACE("IMU timestamp %" PRIu64 " (dt %f) hw2mono local ts %" PRIu64 " (dt %f) offset %" PRId64,
	             device_timestamp_ns,
	             (double)(device_timestamp_ns - t->fusion.last_imu_timestamp_ns) / 1000000000.0, local_timestamp_ns,
	             (double)(local_timestamp_ns - t->fusion.last_imu_local_timestamp_ns) / 1000000000.0, t->hw2mono);

	t->fusion.last_angular_velocity = *gyro;
	t->fusion.last_imu_timestamp_ns = device_timestamp_ns;
	t->fusion.last_imu_local_timestamp_ns = local_timestamp_ns;

	t->pose.orientation = t->fusion.i3dof.rot;

	os_mutex_unlock(&t->mutex);

	if (t->slam_sinks.imu) {
		/* Push IMU sample to the SLAM tracker */
		struct xrt_vec3_f64 accel64 = {accel->x, accel->y, accel->z};
		struct xrt_vec3_f64 gyro64 = {gyro->x, gyro->y, gyro->z};
		struct xrt_imu_sample sample = {
		    .timestamp_ns = local_timestamp_ns, .accel_m_s2 = accel64, .gyro_rad_secs = gyro64};

		xrt_sink_push_imu(t->slam_sinks.imu, &sample);
	}
}

#define UPPER_32BITS(x) ((x)&0xffffffff00000000ULL)

void
rift_s_tracker_push_slam_frames(struct rift_s_tracker *t,
                                uint64_t frame_ts_ns,
                                struct xrt_frame *frames[RIFT_S_CAMERA_COUNT])
{
	timepoint_ns frame_time;

	os_mutex_lock(&t->mutex);

	/* Ignore packets before we're ready */
	if (!t->ready_for_data) {
		os_mutex_unlock(&t->mutex);
		return;
	}

	if (!t->have_hw2mono) {
		/* Drop any frames before we have IMU */
		os_mutex_unlock(&t->mutex);
		return;
	}

	/* Ensure the input timestamp is within 32-bits of the IMU
	 * time, because the timestamps are reported and extended to 64-bits
	 * separately and can end up in different epochs */
	uint64_t adj_frame_ts_ns = frame_ts_ns + t->camera_ts_offset;
	int64_t frame_to_imu_uS = (adj_frame_ts_ns / 1000 - t->fusion.last_imu_timestamp_ns / 1000);

	if (frame_to_imu_uS < -(int64_t)(1ULL << 31) || frame_to_imu_uS > (int64_t)(1ULL << 31)) {
		t->camera_ts_offset =
		    (UPPER_32BITS(t->fusion.last_imu_timestamp_ns / 1000) - UPPER_32BITS(frame_ts_ns / 1000)) * 1000;
		RIFT_S_DEBUG("Applying epoch offset to frame times of %" PRId64 " (frame->imu was %" PRId64 " µS)",
		             t->camera_ts_offset, frame_to_imu_uS);
	}
	frame_ts_ns += t->camera_ts_offset;

	clock_hw2mono_get(t, frame_ts_ns, &frame_time);

	if (frame_time < t->last_frame_time) {
		RIFT_S_WARN("Camera frame time went backward by %" PRId64 " ns", frame_time - t->last_frame_time);
		os_mutex_unlock(&t->mutex);
		return;
	}

	RIFT_S_TRACE("SLAM frame timestamp %" PRIu64 " local %" PRIu64, frame_ts_ns, frame_time);

	t->last_frame_time = frame_time;
	os_mutex_unlock(&t->mutex);

	for (int i = 0; i < RIFT_S_CAMERA_COUNT; i++) {
		if (t->slam_sinks.cams[i]) {
			frames[i]->timestamp = frame_time;
			xrt_sink_push_frame(t->slam_sinks.cams[i], frames[i]);
		}
	}
}

//! Specific pose correction for Basalt to OpenXR coordinates
XRT_MAYBE_UNUSED static inline void
rift_s_tracker_correct_pose_from_basalt(struct xrt_pose *pose)
{
	struct xrt_quat q = {0.70710678, 0, 0, -0.70710678};
	math_quat_rotate(&q, &pose->orientation, &pose->orientation);
	math_quat_rotate_vec3(&q, &pose->position, &pose->position);
}

static void
rift_s_tracker_get_tracked_pose_imu(struct xrt_device *xdev,
                                    enum xrt_input_name name,
                                    uint64_t at_timestamp_ns,
                                    struct xrt_space_relation *out_relation)
{
	struct rift_s_tracker *tracker = (struct rift_s_tracker *)(xdev);
	assert(name == XRT_INPUT_GENERIC_TRACKER_POSE);

	rift_s_tracker_get_tracked_pose(tracker, RIFT_S_TRACKER_POSE_IMU, at_timestamp_ns, out_relation);
}

void
rift_s_tracker_get_tracked_pose(struct rift_s_tracker *t,
                                enum rift_s_tracker_pose pose,
                                uint64_t at_timestamp_ns,
                                struct xrt_space_relation *out_relation)
{
	struct xrt_relation_chain xrc = {0};

	if (pose == RIFT_S_TRACKER_POSE_DEVICE) {
		m_relation_chain_push_pose(&xrc, &t->device_from_imu);
	} else if (pose == RIFT_S_TRACKER_POSE_LEFT_CAMERA) {
		m_relation_chain_push_pose(&xrc, &t->left_cam_from_imu);
	}

	if (t->tracking.slam_enabled && t->slam_over_3dof) {
		struct xrt_space_relation imu_relation = XRT_SPACE_RELATION_ZERO;

		// Get the IMU pose from the SLAM tracker
		xrt_tracked_slam_get_tracked_pose(t->tracking.slam, at_timestamp_ns, &imu_relation);

#if defined(XRT_HAVE_BASALT)
		rift_s_tracker_correct_pose_from_basalt(&imu_relation.pose);
#endif

		imu_relation.relation_flags = (enum xrt_space_relation_flags)(
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
		    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

		m_relation_chain_push_relation(&xrc, &imu_relation);
	} else {
		struct xrt_space_relation imu_relation = XRT_SPACE_RELATION_ZERO;

		os_mutex_lock(&t->mutex);
		// TODO: Estimate pose at timestamp at_timestamp_ns
		math_quat_normalize(&t->pose.orientation);
		imu_relation.pose = t->pose;
		imu_relation.angular_velocity = t->fusion.last_angular_velocity;
		imu_relation.relation_flags = (enum xrt_space_relation_flags)(
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
		    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

		m_relation_chain_push_relation(&xrc, &imu_relation);

		os_mutex_unlock(&t->mutex);
	}

	m_relation_chain_resolve(&xrc, out_relation);
}

void
rift_s_tracker_start(struct rift_s_tracker *t)
{
	os_mutex_lock(&t->mutex);
	t->ready_for_data = true;
	os_mutex_unlock(&t->mutex);
}
