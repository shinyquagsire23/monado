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

#include "util/u_device.h"

#include <librealsense2/rs.h>
#include <librealsense2/h/rs_pipeline.h>
#include <librealsense2/h/rs_option.h>
#include <librealsense2/h/rs_frame.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>



struct rs_6dof
{
	struct xrt_device base;

	struct xrt_pose pose;

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

	fprintf(stderr, "rs_error was raised when calling %s(%s): \n",
	        rs2_get_failed_function(e), rs2_get_failed_args(e));
	fprintf(stderr, "%s\n", rs2_get_error_message(e));

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

	rs2_config_enable_stream(rs->config, RS2_STREAM_POSE, 0, 0, 0,
	                         RS2_FORMAT_6DOF, 200, &e);
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

/*!
 * Process a frame as 6DOF data, does not assume ownership of the frame.
 */
static void
process_frame(struct rs_6dof *rs, rs2_frame *frame, struct xrt_pose *out_pose)
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

	out_pose->orientation.x = camera_pose.rotation.x;
	out_pose->orientation.y = camera_pose.rotation.y;
	out_pose->orientation.z = camera_pose.rotation.z;
	out_pose->orientation.w = camera_pose.rotation.w;

	out_pose->position.x = camera_pose.translation.x;
	out_pose->position.y = camera_pose.translation.y;
	out_pose->position.z = camera_pose.translation.z;
}

static int
update(struct rs_6dof *rs, struct xrt_pose *out_pose)
{
	rs2_frame *frames;
	rs2_error *e = NULL;

	frames =
	    rs2_pipeline_wait_for_frames(rs->pipe, RS2_DEFAULT_TIMEOUT, &e);
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
		process_frame(rs, frame, out_pose);
		rs2_release_frame(frame);

		rs2_release_frame(frames);
		return 0;
	}

	return 0;
}

static void
rs_6dof_update_inputs(struct xrt_device *xdev, struct time_state *timekeeping)
{
	// Empty
}

static void
rs_6dof_get_tracked_pose(struct xrt_device *xdev,
                         enum xrt_input_name name,
                         struct time_state *timekeeping,
                         int64_t *out_timestamp,
                         struct xrt_space_relation *out_relation)
{
	struct rs_6dof *rs = rs_6dof(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		fprintf(stderr, "unknown input name\n");
		return;
	}

	int64_t now = time_state_get_now(timekeeping);
	*out_timestamp = now;

	update(rs, &rs->pose);

	out_relation->pose = rs->pose;
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
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
	close_6dof(rs);
	free(rs);
}

struct xrt_device *
rs_6dof_create(void)
{
	struct rs_6dof *rs = U_DEVICE_ALLOCATE(
	    struct rs_6dof, U_DEVICE_ALLOC_TRACKING_NONE, 1, 0);
	int ret = 0;

	rs->base.update_inputs = rs_6dof_update_inputs;
	rs->base.get_tracked_pose = rs_6dof_get_tracked_pose;
	rs->base.get_view_pose = rs_6dof_get_view_pose;
	rs->base.destroy = rs_6dof_destroy;
	rs->base.name = XRT_DEVICE_GENERIC_HMD; // This is a lie.
	rs->pose.orientation.w = 1.0f;          // All other values set to zero.

	// Print name.
	snprintf(rs->base.str, XRT_DEVICE_NAME_LEN, "Intel RealSense 6-DOF");

	// Setup input, this is a lie.
	rs->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	ret = create_6dof(rs);
	if (ret != 0) {
		rs_6dof_destroy(&rs->base);
		return NULL;
	}

	return &rs->base;
}
