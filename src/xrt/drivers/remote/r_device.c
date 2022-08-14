// Copyright 2020-2022, Collabora, Ltd.
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

#include "vive/vive_bindings.h"

#include "math/m_api.h"

#include "r_internal.h"
#include "util/u_hand_simulation.h"

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
		for (uint32_t i = 0; i < 19; i++) {
			xdev->inputs[i].active = false;
			xdev->inputs[i].timestamp = now;
			U_ZERO(&xdev->inputs[i].value);
		}
		return;
	}

	for (uint32_t i = 0; i < 19; i++) {
		xdev->inputs[i].active = true;
		xdev->inputs[i].timestamp = now;
	}

	// clang-format off
	xdev->inputs[0].value.boolean  = latest->system_click;
	xdev->inputs[1].value.boolean  = latest->system_touch;
	xdev->inputs[2].value.boolean  = latest->a_click;
	xdev->inputs[3].value.boolean  = latest->a_touch;
	xdev->inputs[4].value.boolean  = latest->b_click;
	xdev->inputs[5].value.boolean  = latest->b_touch;
	xdev->inputs[6].value.vec1     = latest->squeeze_value;
	xdev->inputs[7].value.vec1     = latest->squeeze_force;
	xdev->inputs[8].value.boolean  = latest->trigger_click;
	xdev->inputs[9].value.vec1     = latest->trigger_value;
	xdev->inputs[10].value.boolean = latest->trigger_touch;
	xdev->inputs[11].value.vec2    = latest->thumbstick;
	xdev->inputs[12].value.boolean = latest->thumbstick_click;
	xdev->inputs[13].value.boolean = latest->thumbstick_touch;
	xdev->inputs[14].value.vec2    = latest->trackpad;
	xdev->inputs[15].value.vec1    = latest->trackpad_force;
	xdev->inputs[16].value.boolean = latest->trackpad_touch;
	// clang-format on
}

static void
r_device_get_tracked_pose(struct xrt_device *xdev,
                          enum xrt_input_name name,
                          uint64_t at_timestamp_ns,
                          struct xrt_space_relation *out_relation)
{
	struct r_device *rd = r_device(xdev);
	struct r_hub *r = rd->r;

	if (name != XRT_INPUT_INDEX_AIM_POSE && name != XRT_INPUT_INDEX_GRIP_POSE) {
		U_LOG_E("Unknown input name: 0x%0x", name);
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

	if (latest->active) {
		out_relation->relation_flags = (enum xrt_space_relation_flags)(
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
		    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT |
		    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT | XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT);
	} else {
		out_relation->relation_flags = 0;
	}
}

static void
r_device_get_hand_tracking(struct xrt_device *xdev,
                           enum xrt_input_name name,
                           uint64_t requested_timestamp_ns,
                           struct xrt_hand_joint_set *out_value,
                           uint64_t *out_timestamp_ns)
{
	struct r_device *rd = r_device(xdev);
	struct r_hub *r = rd->r;


	if (name != XRT_INPUT_GENERIC_HAND_TRACKING_LEFT && name != XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
		U_LOG_E("Unknown input name for hand tracker: 0x%0x", name);
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

	// Get the pose of the hand.
	struct xrt_space_relation relation;
	xrt_device_get_tracked_pose(xdev, XRT_INPUT_INDEX_GRIP_POSE, requested_timestamp_ns, &relation);

	// Simulate the hand.
	enum xrt_hand hand = rd->is_left ? XRT_HAND_LEFT : XRT_HAND_RIGHT;
	u_hand_sim_simulate_for_valve_index_knuckles(&values, hand, &relation, out_value);

	out_value->is_active = latest->hand_tracking_active;

	// This is a lie
	*out_timestamp_ns = requested_timestamp_ns;
}

static void
r_device_get_view_poses(struct xrt_device *xdev,
                        const struct xrt_vec3 *default_eye_relation,
                        uint64_t at_timestamp_ns,
                        uint32_t view_count,
                        struct xrt_space_relation *out_head_relation,
                        struct xrt_fov *out_fovs,
                        struct xrt_pose *out_poses)
{
	assert(false);
}

static void
r_device_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
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
	const uint32_t input_count = 20; // 19 + hand tracker
	const uint32_t output_count = 1;
	struct r_device *rd = U_DEVICE_ALLOCATE( //
	    struct r_device, flags, input_count, output_count);

	// Setup the basics.
	rd->base.update_inputs = r_device_update_inputs;
	rd->base.get_tracked_pose = r_device_get_tracked_pose;
	rd->base.get_hand_tracking = r_device_get_hand_tracking;
	rd->base.get_view_poses = r_device_get_view_poses;
	rd->base.set_output = r_device_set_output;
	rd->base.destroy = r_device_destroy;
	rd->base.tracking_origin = &r->origin;
	rd->base.orientation_tracking_supported = true;
	rd->base.position_tracking_supported = true;
	rd->base.hand_tracking_supported = true;
	rd->base.name = XRT_DEVICE_INDEX_CONTROLLER;
	rd->base.binding_profiles = vive_binding_profiles_index;
	rd->base.binding_profile_count = vive_binding_profiles_index_count;
	rd->r = r;
	rd->is_left = is_left;

	// Print name.
	snprintf(rd->base.str, sizeof(rd->base.str), "Remote %s Controller", is_left ? "Left" : "Right");
	snprintf(rd->base.serial, sizeof(rd->base.str), "Remote %s Controller", is_left ? "Left" : "Right");



	// Inputs and outputs.
	rd->base.inputs[0].name = XRT_INPUT_INDEX_SYSTEM_CLICK;
	rd->base.inputs[1].name = XRT_INPUT_INDEX_SYSTEM_TOUCH;
	rd->base.inputs[2].name = XRT_INPUT_INDEX_A_CLICK;
	rd->base.inputs[3].name = XRT_INPUT_INDEX_A_TOUCH;
	rd->base.inputs[4].name = XRT_INPUT_INDEX_B_CLICK;
	rd->base.inputs[5].name = XRT_INPUT_INDEX_B_TOUCH;
	rd->base.inputs[6].name = XRT_INPUT_INDEX_SQUEEZE_VALUE;
	rd->base.inputs[7].name = XRT_INPUT_INDEX_SQUEEZE_FORCE;
	rd->base.inputs[8].name = XRT_INPUT_INDEX_TRIGGER_CLICK;
	rd->base.inputs[9].name = XRT_INPUT_INDEX_TRIGGER_VALUE;
	rd->base.inputs[10].name = XRT_INPUT_INDEX_TRIGGER_TOUCH;
	rd->base.inputs[11].name = XRT_INPUT_INDEX_THUMBSTICK;
	rd->base.inputs[12].name = XRT_INPUT_INDEX_THUMBSTICK_CLICK;
	rd->base.inputs[13].name = XRT_INPUT_INDEX_THUMBSTICK_TOUCH;
	rd->base.inputs[14].name = XRT_INPUT_INDEX_TRACKPAD;
	rd->base.inputs[15].name = XRT_INPUT_INDEX_TRACKPAD_FORCE;
	rd->base.inputs[16].name = XRT_INPUT_INDEX_TRACKPAD_TOUCH;
	rd->base.inputs[17].name = XRT_INPUT_INDEX_GRIP_POSE;
	rd->base.inputs[18].name = XRT_INPUT_INDEX_AIM_POSE;
	if (is_left) {
		rd->base.inputs[19].name = XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
	} else {
		rd->base.inputs[19].name = XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;
	}
	rd->base.outputs[0].name = XRT_OUTPUT_NAME_INDEX_HAPTIC;

	if (is_left) {
		rd->base.device_type = XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;
	} else {
		rd->base.device_type = XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER;
	}

	// Setup variable tracker.
	u_var_add_root(rd, rd->base.str, true);

	return &rd->base;
}
