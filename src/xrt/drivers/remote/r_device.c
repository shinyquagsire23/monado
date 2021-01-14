// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Controller remote driver.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_remote
 */

#include "os/os_time.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_hand_tracking.h"

#include "math/m_api.h"

#include "r_internal.h"

#include <stdio.h>


/*
 *
 * Functions
 *
 */

static inline struct r_device *
r_device(struct xrt_device *xdev)
{
	return (struct r_device *)xdev;
}

static void
r_device_destroy(struct xrt_device *xdev)
{
	struct r_device *rd = r_device(xdev);

	// Remove the variable tracking.
	u_var_remove_root(rd);

	// Free this device with the helper.
	u_device_free(&rd->base);
}

static void
r_device_update_inputs(struct xrt_device *xdev)
{
	struct r_device *rd = r_device(xdev);
	struct r_hub *r = rd->r;

	uint64_t now = os_monotonic_get_ns();
	struct r_remote_controller_data *latest = rd->is_left ? &r->latest.left : &r->latest.right;

	if (!latest->active) {
		xdev->inputs[0].active = false;
		xdev->inputs[0].timestamp = now;
		xdev->inputs[1].active = false;
		xdev->inputs[1].timestamp = now;
		xdev->inputs[2].active = false;
		xdev->inputs[2].timestamp = now;
		xdev->inputs[3].active = false;
		xdev->inputs[3].timestamp = now;
	} else {
		xdev->inputs[0].active = true;
		xdev->inputs[0].timestamp = now;
		xdev->inputs[0].value.boolean = latest->select;
		xdev->inputs[1].active = true;
		xdev->inputs[1].timestamp = now;
		xdev->inputs[1].value.boolean = latest->menu;
		xdev->inputs[2].active = true;
		xdev->inputs[2].timestamp = now;
		xdev->inputs[3].active = true;
		xdev->inputs[3].timestamp = now;
	}
}

static void
r_device_get_tracked_pose(struct xrt_device *xdev,
                          enum xrt_input_name name,
                          uint64_t at_timestamp_ns,
                          struct xrt_space_relation *out_relation)
{
	struct r_device *rd = r_device(xdev);
	struct r_hub *r = rd->r;

	if (name != XRT_INPUT_SIMPLE_AIM_POSE && name != XRT_INPUT_SIMPLE_GRIP_POSE) {
		U_LOG_E("Unknown input name");
		return;
	}

	struct r_remote_controller_data *latest = rd->is_left ? &r->latest.left : &r->latest.right;

	/*
	 * It's easier to reason about angular velocity if it's controlled in
	 * body space, but the angular velocity returned in the relation is in
	 * the base space.
	 */
	math_quat_rotate_derivative(&latest->pose.orientation, &latest->angular_velocity,
	                            &out_relation->angular_velocity);

	out_relation->pose = latest->pose;
	out_relation->linear_velocity = latest->linear_velocity;
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT |
	    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT | XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT);
}

static void
r_device_get_hand_tracking(struct xrt_device *xdev,
                           enum xrt_input_name name,
                           uint64_t at_timestamp_ns,
                           struct xrt_hand_joint_set *out_value)
{
	struct r_device *rd = r_device(xdev);
	struct r_hub *r = rd->r;


	if (name != XRT_INPUT_GENERIC_HAND_TRACKING_LEFT && name != XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
		U_LOG_E("Unknown input name for hand tracker");
		return;
	}

	struct r_remote_controller_data *latest = rd->is_left ? &r->latest.left : &r->latest.right;

	struct u_hand_tracking_curl_values values = {
	    .little = latest->hand_curl[0],
	    .ring = latest->hand_curl[1],
	    .middle = latest->hand_curl[2],
	    .index = latest->hand_curl[3],
	    .thumb = latest->hand_curl[4],
	};

	enum xrt_hand hand = rd->is_left ? XRT_HAND_LEFT : XRT_HAND_RIGHT;
	u_hand_joints_update_curl(&rd->hand_tracking, hand, at_timestamp_ns, &values);

	struct xrt_pose hand_on_handle_pose = {
	    {0, 0, 0, 1},
	    {0, 0, 0},
	};

	struct xrt_space_relation relation;
	xrt_device_get_tracked_pose(xdev, XRT_INPUT_SIMPLE_GRIP_POSE, at_timestamp_ns, &relation);

	u_hand_joints_set_out_data(&rd->hand_tracking, hand, &relation, &hand_on_handle_pose, out_value);
}

static void
r_device_get_view_pose(struct xrt_device *xdev,
                       struct xrt_vec3 *eye_relation,
                       uint32_t view_index,
                       struct xrt_pose *out_pose)
{
	// Empty
}

static void
r_device_set_output(struct xrt_device *xdev, enum xrt_output_name name, union xrt_output_value *value)
{
	struct r_device *rd = r_device(xdev);
	(void)rd;
}

/*!
 * @public @memberof r_device
 */
struct xrt_device *
r_device_create(struct r_hub *r, bool is_left)
{
	// Allocate.
	const enum u_device_alloc_flags flags = 0;
	const uint32_t num_inputs = 5;
	const uint32_t num_outputs = 1;
	struct r_device *rd = U_DEVICE_ALLOCATE( //
	    struct r_device, flags, num_inputs, num_outputs);

	// Setup the basics.
	rd->base.update_inputs = r_device_update_inputs;
	rd->base.get_tracked_pose = r_device_get_tracked_pose;
	rd->base.get_hand_tracking = r_device_get_hand_tracking;
	rd->base.get_view_pose = r_device_get_view_pose;
	rd->base.set_output = r_device_set_output;
	rd->base.destroy = r_device_destroy;
	rd->base.tracking_origin = &r->base;
	rd->base.orientation_tracking_supported = true;
	rd->base.position_tracking_supported = true;
	rd->base.hand_tracking_supported = true;
	rd->base.name = XRT_DEVICE_SIMPLE_CONTROLLER;
	rd->r = r;
	rd->is_left = is_left;

	// Print name.
	snprintf(rd->base.str, sizeof(rd->base.str), "Remote %s Controller", is_left ? "Left" : "Right");

	// Inputs and outputs.
	rd->base.inputs[0].name = XRT_INPUT_SIMPLE_SELECT_CLICK;
	rd->base.inputs[1].name = XRT_INPUT_SIMPLE_MENU_CLICK;
	rd->base.inputs[2].name = XRT_INPUT_SIMPLE_GRIP_POSE;
	rd->base.inputs[3].name = XRT_INPUT_SIMPLE_AIM_POSE;
	if (is_left) {
		rd->base.inputs[4].name = XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
	} else {
		rd->base.inputs[4].name = XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;
	}
	rd->base.outputs[0].name = XRT_OUTPUT_NAME_SIMPLE_VIBRATION;

	if (is_left) {
		rd->base.device_type = XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;
	} else {
		rd->base.device_type = XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER;
	}

	enum xrt_hand hand = rd->is_left ? XRT_HAND_LEFT : XRT_HAND_RIGHT;
	u_hand_joints_init_default_set(&rd->hand_tracking, hand, XRT_HAND_TRACKING_MODEL_FINGERL_CURL, 1.0);

	// Setup variable tracker.
	u_var_add_root(rd, rd->base.str, true);

	return &rd->base;
}
