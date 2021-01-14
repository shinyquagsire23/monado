// Copyright 2020, Collabora, Ltd.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  RealSense helper driver for 6DOF tracking.
 * @author Nova King <technobaboo@gmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_rs
 */

#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "math/m_api.h"
#include "math/m_space.h"
#include "math/m_predict.h"

#include "os/os_time.h"
#include "os/os_threading.h"

#include "util/u_time.h"
#include "util/u_device.h"
#include "util/u_logging.h"

#include <librealsense2/rs.h>
#include <librealsense2/h/rs_pipeline.h>
#include <librealsense2/h/rs_option.h>
#include <librealsense2/h/rs_frame.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>


/*!
 * Stupid convenience macro to print out a pose, only used for debugging
 */
#define print_pose(msg, pose)                                                                                          \
	U_LOG_E(msg " %f %f %f  %f %f %f %f", pose.position.x, pose.position.y, pose.position.z, pose.orientation.x,   \
	        pose.orientation.y, pose.orientation.z, pose.orientation.w)

/*!
 * @implements xrt_device
 */
struct rs_6dof
{
	struct xrt_device base;

	uint64_t relation_timestamp_ns;
	struct xrt_space_relation relation;

	// arbitrary offset to apply to the pose the t265 gives us
	struct xrt_pose offset;

	struct os_thread_helper oth;

	rs2_context *ctx;
	rs2_pipeline *pipe;
	rs2_pipeline_profile *profile;
	rs2_config *config;
};


/*!
 * Helper to convert a xdev to a @ref rs_6dof.
 */
static inline struct rs_6dof *
rs_6dof(struct xrt_device *xdev)
{
	return (struct rs_6dof *)xdev;
}

/*!
 * Simple helper to check and print error messages.
 */
static int
check_error(struct rs_6dof *rs, rs2_error *e)
{
	if (e == NULL) {
		return 0;
	}

	U_LOG_E("rs_error was raised when calling %s(%s):", rs2_get_failed_function(e), rs2_get_failed_args(e));
	U_LOG_E("%s", rs2_get_error_message(e));

	return 1;
}

/*!
 * Frees all RealSense resources.
 */
static void
close_6dof(struct rs_6dof *rs)
{
	if (rs->config) {
		rs2_delete_config(rs->config);
		rs->config = NULL;
	}

	if (rs->profile) {
		rs2_delete_pipeline_profile(rs->profile);
		rs->profile = NULL;
	}

	if (rs->pipe) {
		rs2_pipeline_stop(rs->pipe, NULL);
		rs2_delete_pipeline(rs->pipe);
		rs->pipe = NULL;
	}

	if (rs->ctx) {
		rs2_delete_context(rs->ctx);
		rs->ctx = NULL;
	}
}

/*!
 * Create all RealSense resources needed for 6DOF tracking.
 */
static int
create_6dof(struct rs_6dof *rs)
{
	assert(rs != NULL);
	rs2_error *e = NULL;

	rs->ctx = rs2_create_context(RS2_API_VERSION, &e);
	if (check_error(rs, e) != 0) {
		close_6dof(rs);
		return 1;
	}

	rs->pipe = rs2_create_pipeline(rs->ctx, &e);
	if (check_error(rs, e) != 0) {
		close_6dof(rs);
		return 1;
	}

	rs->config = rs2_create_config(&e);
	if (check_error(rs, e) != 0) {
		close_6dof(rs);
		return 1;
	}

	rs2_config_enable_stream(rs->config,      //
	                         RS2_STREAM_POSE, // Type
	                         0,               // Index
	                         0,               // Width
	                         0,               // Height
	                         RS2_FORMAT_6DOF, // Format
	                         200,             // FPS
	                         &e);
	if (check_error(rs, e) != 0) {
		close_6dof(rs);
		return 1;
	}

	rs->profile = rs2_pipeline_start_with_config(rs->pipe, rs->config, &e);
	if (check_error(rs, e) != 0) {
		close_6dof(rs);
		return 1;
	}

	return 0;
}


void
rs_update_offset(struct xrt_pose offset, struct xrt_device *xdev)
{
	struct rs_6dof *rs = rs_6dof(xdev);
	memcpy(&rs->offset, &offset, sizeof(struct xrt_pose));
}


/*!
 * Process a frame as 6DOF data, does not assume ownership of the frame.
 */
static void
process_frame(struct rs_6dof *rs, rs2_frame *frame)
{
	rs2_error *e = NULL;
	int ret = 0;

	ret = rs2_is_frame_extendable_to(frame, RS2_EXTENSION_POSE_FRAME, &e);
	if (check_error(rs, e) != 0 || ret == 0) {
		return;
	}

	rs2_pose camera_pose;
	rs2_pose_frame_get_pose_data(frame, &camera_pose, &e);
	if (check_error(rs, e) != 0) {
		return;
	}

#if 0
	rs2_timestamp_domain domain = rs2_get_frame_timestamp_domain(frame, &e);
	if (check_error(rs, e) != 0) {
		return;
	}
#endif

	double timestamp_miliseconds = rs2_get_frame_timestamp(frame, &e);
	if (check_error(rs, e) != 0) {
		return;
	}

	// Close enough
	uint64_t now_real_ns = os_realtime_get_ns();
	uint64_t now_monotonic_ns = os_monotonic_get_ns();
	uint64_t timestamp_ns = (uint64_t)(timestamp_miliseconds * 1000.0 * 1000.0);

	// How far in the past is it?
	uint64_t diff_ns = now_real_ns - timestamp_ns;

	// Adjust the timestamp to monotonic time.
	timestamp_ns = now_monotonic_ns - diff_ns;

	/*
	 * Transfer the data to the struct.
	 */

	// Re-use the thread lock for the data.
	os_thread_helper_lock(&rs->oth);

	// clang-format off
	// Timestamp
	rs->relation_timestamp_ns = timestamp_ns;

	// Rotation/angular
	rs->relation.pose.orientation.x = camera_pose.rotation.x;
	rs->relation.pose.orientation.y = camera_pose.rotation.y;
	rs->relation.pose.orientation.z = camera_pose.rotation.z;
	rs->relation.pose.orientation.w = camera_pose.rotation.w;
	rs->relation.angular_velocity.x = camera_pose.angular_velocity.x;
	rs->relation.angular_velocity.y = camera_pose.angular_velocity.y;
	rs->relation.angular_velocity.z = camera_pose.angular_velocity.z;

	// Position/linear
	rs->relation.pose.position.x = camera_pose.translation.x;
	rs->relation.pose.position.y = camera_pose.translation.y;
	rs->relation.pose.position.z = camera_pose.translation.z;
	rs->relation.linear_velocity.x = camera_pose.velocity.x;
	rs->relation.linear_velocity.y = camera_pose.velocity.y;
	rs->relation.linear_velocity.z = camera_pose.velocity.z;

	rs->relation.relation_flags =
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT |
	    XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
	// clang-format on

	// Re-use the thread lock for the data.
	os_thread_helper_unlock(&rs->oth);
}

static int
update(struct rs_6dof *rs)
{
	rs2_frame *frames;
	rs2_error *e = NULL;

	frames = rs2_pipeline_wait_for_frames(rs->pipe, RS2_DEFAULT_TIMEOUT, &e);
	if (check_error(rs, e) != 0) {
		return 1;
	}

	int num_frames = rs2_embedded_frames_count(frames, &e);
	if (check_error(rs, e) != 0) {
		return 1;
	}

	for (int i = 0; i < num_frames; i++) {
		rs2_frame *frame = rs2_extract_frame(frames, i, &e);
		if (check_error(rs, e) != 0) {
			rs2_release_frame(frames);
			return 1;
		}

		// Does not assume ownership of the frame.
		process_frame(rs, frame);
		rs2_release_frame(frame);

		rs2_release_frame(frames);
	}

	return 0;
}

static void *
rs_run_thread(void *ptr)
{
	struct rs_6dof *rs = (struct rs_6dof *)ptr;

	os_thread_helper_lock(&rs->oth);

	while (os_thread_helper_is_running_locked(&rs->oth)) {

		os_thread_helper_unlock(&rs->oth);

		int ret = update(rs);
		if (ret < 0) {
			return NULL;
		}
	}

	return NULL;
}

static void
rs_6dof_update_inputs(struct xrt_device *xdev)
{
	// Empty
}

static void
rs_6dof_get_tracked_pose(struct xrt_device *xdev,
                         enum xrt_input_name name,
                         uint64_t at_timestamp_ns,
                         struct xrt_space_relation *out_relation)
{
	struct rs_6dof *rs = rs_6dof(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		U_LOG_E("unknown input name");
		return;
	}

	os_thread_helper_lock(&rs->oth);
	struct xrt_space_relation relation_not_predicted = rs->relation;
	uint64_t relation_timestamp_ns = rs->relation_timestamp_ns;
	os_thread_helper_unlock(&rs->oth);

	int64_t diff_prediction_ns = 0;
	diff_prediction_ns = at_timestamp_ns - relation_timestamp_ns;
	struct xrt_space_relation relation;

	double delta_s = time_ns_to_s(diff_prediction_ns);
	m_predict_relation(&relation_not_predicted, delta_s, &relation);

	struct xrt_space_graph xsg = {0};
	m_space_graph_add_pose(&xsg, &rs->offset);
	m_space_graph_add_relation(&xsg, &relation);
	m_space_graph_resolve(&xsg, out_relation);
}
static void
rs_6dof_get_view_pose(struct xrt_device *xdev,
                      struct xrt_vec3 *eye_relation,
                      uint32_t view_index,
                      struct xrt_pose *out_pose)
{
	assert(false);
}

static void
rs_6dof_destroy(struct xrt_device *xdev)
{
	struct rs_6dof *rs = rs_6dof(xdev);

	// Destroy the thread object.
	os_thread_helper_destroy(&rs->oth);

	close_6dof(rs);

	free(rs);
}

struct xrt_device *
rs_6dof_create(void)
{
	struct rs_6dof *rs = U_DEVICE_ALLOCATE(struct rs_6dof, U_DEVICE_ALLOC_TRACKING_NONE, 1, 0);
	int ret = 0;

	rs->base.update_inputs = rs_6dof_update_inputs;
	rs->base.get_tracked_pose = rs_6dof_get_tracked_pose;
	rs->base.get_view_pose = rs_6dof_get_view_pose;
	rs->base.destroy = rs_6dof_destroy;
	rs->base.name = XRT_DEVICE_GENERIC_HMD; // This is a lie.
	rs->relation.pose.orientation.w = 1.0f; // All other values set to zero.

	rs->base.tracking_origin->type = XRT_TRACKING_TYPE_EXTERNAL_SLAM;

	// Print name.
	snprintf(rs->base.str, XRT_DEVICE_NAME_LEN, "Intel RealSense 6-DOF");

	// Setup input, this is a lie.
	rs->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	// Thread and other state.
	ret = os_thread_helper_init(&rs->oth);
	if (ret != 0) {
		U_LOG_E("Failed to init threading!");
		rs_6dof_destroy(&rs->base);
		return NULL;
	}

	ret = create_6dof(rs);
	if (ret != 0) {
		rs_6dof_destroy(&rs->base);
		return NULL;
	}

	ret = os_thread_helper_start(&rs->oth, rs_run_thread, rs);
	if (ret != 0) {
		U_LOG_E("Failed to start thread!");
		rs_6dof_destroy(&rs->base);
		return NULL;
	}

	rs->base.orientation_tracking_supported = true;
	rs->base.position_tracking_supported = true;
	rs->base.device_type = XRT_DEVICE_TYPE_GENERIC_TRACKER;

	return &rs->base;
}
