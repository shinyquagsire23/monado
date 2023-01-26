// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simulated controller device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_simulated
 */

#include "xrt/xrt_device.h"

#include "os/os_time.h"

#include "math/m_api.h"
#include "math/m_mathinclude.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_logging.h"
#include "util/u_distortion_mesh.h"

#include "simulated_interface.h"

#include <stdio.h>
#include <assert.h>


/*
 *
 * Structs and defines.
 *
 */

struct simulated_device
{
	struct xrt_device base;

	struct xrt_pose center;

	bool active;
};

#define CHECK_THAT_NAME_IS_AND_ERROR(NAME)                                                                             \
	do {                                                                                                           \
		if (sd->base.name != NAME) {                                                                           \
			U_LOG_E("Unknown input for controller %s 0x%02x", #NAME, name);                                \
			return;                                                                                        \
		}                                                                                                      \
	} while (false)


/*
 *
 * Helper functions.
 *
 */

static inline struct simulated_device *
simulated_device(struct xrt_device *xdev)
{
	return (struct simulated_device *)xdev;
}

static const char *
device_type_to_printable_handedness(enum xrt_device_type type)
{
	switch (type) {
	case XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER: return " Left";
	case XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER: return " Right";
	default: assert(false && "Must be valid handedness"); return NULL;
	}
}


/*
 *
 * Member functions.
 *
 */

static void
simulated_device_destroy(struct xrt_device *xdev)
{
	struct simulated_device *sd = simulated_device(xdev);

	// Remove the variable tracking.
	u_var_remove_root(sd);

	// Free this device with the helper.
	u_device_free(&sd->base);
}

static void
simulated_device_update_inputs(struct xrt_device *xdev)
{
	struct simulated_device *sd = simulated_device(xdev);

	uint64_t now = os_monotonic_get_ns();

	if (!sd->active) {
		for (uint32_t i = 0; i < xdev->input_count; i++) {
			xdev->inputs[i].active = false;
			xdev->inputs[i].timestamp = now;
			U_ZERO(&xdev->inputs[i].value);
		}
		return;
	}

	for (uint32_t i = 0; i < xdev->input_count; i++) {
		xdev->inputs[i].active = true;
		xdev->inputs[i].timestamp = now;
	}
}

static void
simulated_device_get_tracked_pose(struct xrt_device *xdev,
                                  enum xrt_input_name name,
                                  uint64_t at_timestamp_ns,
                                  struct xrt_space_relation *out_relation)
{
	struct simulated_device *sd = simulated_device(xdev);

	switch (name) {
	case XRT_INPUT_SIMPLE_GRIP_POSE:
	case XRT_INPUT_SIMPLE_AIM_POSE: CHECK_THAT_NAME_IS_AND_ERROR(XRT_DEVICE_SIMPLE_CONTROLLER); break;
	case XRT_INPUT_WMR_GRIP_POSE:
	case XRT_INPUT_WMR_AIM_POSE: CHECK_THAT_NAME_IS_AND_ERROR(XRT_DEVICE_WMR_CONTROLLER); break;
	case XRT_INPUT_ML2_CONTROLLER_GRIP_POSE:
	case XRT_INPUT_ML2_CONTROLLER_AIM_POSE: CHECK_THAT_NAME_IS_AND_ERROR(XRT_DEVICE_ML2_CONTROLLER); break;
	default: U_LOG_E("Unknown input name: 0x%0x", name); return;
	}

	if (!sd->active) {
		out_relation->pose = (struct xrt_pose)XRT_POSE_IDENTITY;
		out_relation->relation_flags = 0;
		return;
	}

	struct xrt_pose pose = sd->center;
	struct xrt_vec3 linear_velocity = XRT_VEC3_ZERO;
	struct xrt_vec3 angular_velocity = XRT_VEC3_ZERO;

	/*
	 * It's easier to reason about angular velocity if it's controlled in
	 * body space, but the angular velocity returned in the relation is in
	 * the base space.
	 */
	math_quat_rotate_derivative(&pose.orientation, &angular_velocity, &out_relation->angular_velocity);

	out_relation->pose = pose;
	out_relation->linear_velocity = linear_velocity;

	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT |
	    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT | XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT);
}

static void
simulated_device_get_hand_tracking(struct xrt_device *xdev,
                                   enum xrt_input_name name,
                                   uint64_t requested_timestamp_ns,
                                   struct xrt_hand_joint_set *out_value,
                                   uint64_t *out_timestamp_ns)
{
	assert(false);
}

static void
simulated_device_get_view_poses(struct xrt_device *xdev,
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
simulated_device_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	struct simulated_device *sd = simulated_device(xdev);
	(void)sd;
}


/*
 *
 * Various data driven arrays.
 *
 */

/*
 * Simple Controller.
 */

static enum xrt_input_name simple_inputs_array[] = {
    XRT_INPUT_SIMPLE_SELECT_CLICK,
    XRT_INPUT_SIMPLE_MENU_CLICK,
    XRT_INPUT_SIMPLE_GRIP_POSE,
    XRT_INPUT_SIMPLE_AIM_POSE,
};

static enum xrt_output_name simple_outputs_array[] = {
    XRT_OUTPUT_NAME_SIMPLE_VIBRATION,
};


/*
 * WinMR Controller.
 */

static enum xrt_input_name wmr_inputs_array[] = {
    XRT_INPUT_WMR_MENU_CLICK,       XRT_INPUT_WMR_SQUEEZE_CLICK, XRT_INPUT_WMR_TRIGGER_VALUE,
    XRT_INPUT_WMR_THUMBSTICK_CLICK, XRT_INPUT_WMR_THUMBSTICK,    XRT_INPUT_WMR_TRACKPAD_CLICK,
    XRT_INPUT_WMR_TRACKPAD_TOUCH,   XRT_INPUT_WMR_TRACKPAD,      XRT_INPUT_WMR_GRIP_POSE,
    XRT_INPUT_WMR_AIM_POSE,
};

static enum xrt_output_name wmr_outputs_array[] = {
    XRT_OUTPUT_NAME_WMR_HAPTIC,
};

static struct xrt_binding_input_pair wmr_to_simple_inputs[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_WMR_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_WMR_MENU_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_WMR_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_WMR_AIM_POSE},
};

static struct xrt_binding_output_pair wmr_to_simple_outputs[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_INDEX_HAPTIC},
};

static struct xrt_binding_profile wmr_binding_profiles[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = wmr_to_simple_inputs,
        .input_count = ARRAY_SIZE(wmr_to_simple_inputs),
        .outputs = wmr_to_simple_outputs,
        .output_count = ARRAY_SIZE(wmr_to_simple_outputs),
    },
};


/*
 * ML2 Controller.
 */

static enum xrt_input_name ml2_inputs_array[] = {
    XRT_INPUT_ML2_CONTROLLER_MENU_CLICK,     XRT_INPUT_ML2_CONTROLLER_SELECT_CLICK,
    XRT_INPUT_ML2_CONTROLLER_TRIGGER_CLICK,  XRT_INPUT_ML2_CONTROLLER_TRIGGER_VALUE,
    XRT_INPUT_ML2_CONTROLLER_TRACKPAD_CLICK, XRT_INPUT_ML2_CONTROLLER_TRACKPAD_TOUCH,
    XRT_INPUT_ML2_CONTROLLER_TRACKPAD_FORCE, XRT_INPUT_ML2_CONTROLLER_TRACKPAD,
    XRT_INPUT_ML2_CONTROLLER_GRIP_POSE,      XRT_INPUT_ML2_CONTROLLER_AIM_POSE,
    XRT_INPUT_ML2_CONTROLLER_SHOULDER_CLICK,
};

static enum xrt_output_name ml2_outputs_array[] = {
    XRT_OUTPUT_NAME_ML2_CONTROLLER_VIBRATION,
};

static struct xrt_binding_input_pair ml2_to_simple_inputs[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_ML2_CONTROLLER_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_ML2_CONTROLLER_MENU_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_ML2_CONTROLLER_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_ML2_CONTROLLER_AIM_POSE},
};

static struct xrt_binding_output_pair ml2_to_simple_outputs[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_INDEX_HAPTIC},
};

static struct xrt_binding_input_pair ml2_to_vive_wand_inputs[9] = {
    {XRT_INPUT_VIVE_GRIP_POSE, XRT_INPUT_ML2_CONTROLLER_GRIP_POSE},
    {XRT_INPUT_VIVE_AIM_POSE, XRT_INPUT_ML2_CONTROLLER_AIM_POSE},
    {XRT_INPUT_VIVE_TRIGGER_CLICK, XRT_INPUT_ML2_CONTROLLER_TRIGGER_CLICK},
    {XRT_INPUT_VIVE_TRIGGER_VALUE, XRT_INPUT_ML2_CONTROLLER_TRIGGER_VALUE},
    {XRT_INPUT_VIVE_SQUEEZE_CLICK, XRT_INPUT_ML2_CONTROLLER_SHOULDER_CLICK},
    // {XRT_INPUT_VIVE_SYSTEM_CLICK, NONE},
    {XRT_INPUT_VIVE_MENU_CLICK, XRT_INPUT_ML2_CONTROLLER_MENU_CLICK},
    {XRT_INPUT_VIVE_TRACKPAD, XRT_INPUT_ML2_CONTROLLER_TRACKPAD},
    //  {NONE, XRT_INPUT_ML2_CONTROLLER_TRACKPAD_FORCE},
    {XRT_INPUT_VIVE_TRACKPAD_TOUCH, XRT_INPUT_ML2_CONTROLLER_TRACKPAD_TOUCH},
    {XRT_INPUT_VIVE_TRACKPAD_CLICK, XRT_INPUT_ML2_CONTROLLER_TRACKPAD_CLICK},
};

static struct xrt_binding_output_pair ml2_to_vive_wand_outputs[1] = {
    {XRT_OUTPUT_NAME_VIVE_HAPTIC, XRT_OUTPUT_NAME_ML2_CONTROLLER_VIBRATION},
};

static struct xrt_binding_profile ml2_binding_profiles[2] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = ml2_to_simple_inputs,
        .input_count = ARRAY_SIZE(ml2_to_simple_inputs),
        .outputs = ml2_to_simple_outputs,
        .output_count = ARRAY_SIZE(ml2_to_simple_outputs),
    },
    {
        .name = XRT_DEVICE_VIVE_WAND,
        .inputs = ml2_to_vive_wand_inputs,
        .input_count = ARRAY_SIZE(ml2_to_vive_wand_inputs),
        .outputs = ml2_to_vive_wand_outputs,
        .output_count = ARRAY_SIZE(ml2_to_vive_wand_outputs),
    },
};


/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_device *
simulated_create_controller(enum xrt_device_name name,
                            enum xrt_device_type type,
                            const struct xrt_pose *center,
                            struct xrt_tracking_origin *origin)
{
	const enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	const char *handedness = "";
	const char *name_str = NULL;
	enum xrt_input_name *inputs = NULL;
	uint32_t input_count = 0;
	enum xrt_output_name *outputs = NULL;
	uint32_t output_count = 0;
	struct xrt_binding_profile *binding_profiles = NULL;
	uint32_t binding_profile_count = 0;

	switch (name) {
	case XRT_DEVICE_SIMPLE_CONTROLLER:
		name_str = "Simple";
		input_count = ARRAY_SIZE(simple_inputs_array);
		output_count = ARRAY_SIZE(simple_outputs_array);
		inputs = simple_inputs_array;
		outputs = simple_outputs_array;
		assert(type == XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER);
		break;
	case XRT_DEVICE_WMR_CONTROLLER:
		name_str = "WinMR";
		input_count = ARRAY_SIZE(wmr_inputs_array);
		output_count = ARRAY_SIZE(wmr_outputs_array);
		inputs = wmr_inputs_array;
		outputs = wmr_outputs_array;
		binding_profiles = wmr_binding_profiles;
		binding_profile_count = ARRAY_SIZE(wmr_binding_profiles);
		handedness = device_type_to_printable_handedness(type);
		break;
	case XRT_DEVICE_ML2_CONTROLLER:
		name_str = "ML2";
		input_count = ARRAY_SIZE(ml2_inputs_array);
		output_count = ARRAY_SIZE(ml2_outputs_array);
		inputs = ml2_inputs_array;
		outputs = ml2_outputs_array;
		binding_profiles = ml2_binding_profiles;
		binding_profile_count = ARRAY_SIZE(ml2_binding_profiles);

		assert(type == XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER);
		break;
	default: assert(false); return NULL;
	}

	// Allocate.
	struct simulated_device *sd = U_DEVICE_ALLOCATE(struct simulated_device, flags, input_count, output_count);
	sd->base.update_inputs = simulated_device_update_inputs;
	sd->base.get_tracked_pose = simulated_device_get_tracked_pose;
	sd->base.get_hand_tracking = simulated_device_get_hand_tracking;
	sd->base.get_view_poses = simulated_device_get_view_poses;
	sd->base.set_output = simulated_device_set_output;
	sd->base.destroy = simulated_device_destroy;
	sd->base.tracking_origin = origin;
	sd->base.orientation_tracking_supported = true;
	sd->base.position_tracking_supported = true;
	sd->base.hand_tracking_supported = false;
	sd->base.name = name;
	sd->base.device_type = type;
	sd->base.binding_profiles = binding_profiles;
	sd->base.binding_profile_count = binding_profile_count;

	snprintf(sd->base.str, sizeof(sd->base.str), "%s%s Controller (Simulated)", name_str, handedness);
	snprintf(sd->base.serial, sizeof(sd->base.str), "%s%s Controller (Simulated)", name_str, handedness);

	for (uint32_t i = 0; i < input_count; i++) {
		sd->base.inputs[i].active = true;
		sd->base.inputs[i].name = inputs[i];
	}

	for (uint32_t i = 0; i < output_count; i++) {
		sd->base.outputs[i].name = outputs[i];
	}

	sd->center = *center;
	sd->active = true;

	u_var_add_root(sd, sd->base.str, true);
	u_var_add_pose(sd, &sd->center, "center");
	u_var_add_bool(sd, &sd->active, "active");

	return &sd->base;
}
