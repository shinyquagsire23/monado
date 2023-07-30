// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vive poses implementation
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup aux_vive
 */

#include "vive_poses.h"

#include "math/m_mathinclude.h"
#include "math/m_api.h"

#define DEG_TO_RAD(DEG) (DEG * M_PI / 180.)

static void
vive_poses_apply_right_transform(struct xrt_vec3 *out_transform_position, struct xrt_vec3 *out_transform_rotation)
{
	out_transform_rotation->y *= -1.f;
	out_transform_rotation->z *= -1.f;

	out_transform_position->x *= -1.f;
}

static void
vive_poses_get_index_offset_euler(const enum xrt_input_name input_name,
                                  const enum xrt_device_type device_type,
                                  struct xrt_vec3 *out_transform_position,
                                  struct xrt_vec3 *out_transform_rotation)
{
	switch (input_name) {
	case XRT_INPUT_INDEX_GRIP_POSE:
		out_transform_position->x = 0.f;
		out_transform_position->y = -0.015f;
		out_transform_position->z = 0.13f;

		out_transform_rotation->x = DEG_TO_RAD(15.392f);
		out_transform_rotation->y = DEG_TO_RAD(-2.071f);
		out_transform_rotation->z = DEG_TO_RAD(0.303);

		break;
	case XRT_INPUT_INDEX_AIM_POSE:
		out_transform_position->x = 0.006f;
		out_transform_position->y = -0.015f;
		out_transform_position->z = 0.02f;

		out_transform_rotation->x = DEG_TO_RAD(-40.f);
		out_transform_rotation->y = DEG_TO_RAD(-5.f);
		out_transform_rotation->z = 0.f;

		break;
	default:
		*out_transform_position = (struct xrt_vec3)XRT_VEC3_ZERO;
		*out_transform_rotation = (struct xrt_vec3)XRT_VEC3_ZERO;

		break;
	}

	if (device_type == XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER) {
		vive_poses_apply_right_transform(out_transform_position, out_transform_rotation);
	}
}

static void
vive_poses_get_index_hand_offset_pose(enum xrt_hand hand, struct xrt_pose *offset)
{
	/* Controller space origin is at the very tip of the controller,
	 * handle pointing forward at -z.
	 *
	 * Transform joints into controller space by rotating "outwards" around
	 * -z "forward" by -75/75 deg. Then, rotate "forward" around x by 72
	 * deg.
	 *
	 * Then position everything at static_offset..
	 *
	 * Now the hand points "through the strap" like at normal use.
	 */
	struct xrt_vec3 offset_position = {.x = 0, .y = 0.05, .z = 0.11};
#if 0
	const struct xrt_vec3 x = XRT_VEC3_UNIT_X;
	const struct xrt_vec3 y = XRT_VEC3_UNIT_Y;
	const struct xrt_vec3 negative_z = {0, 0, -1};

	float hand_on_handle_x_rotation = DEG_TO_RAD(-72);
	float hand_on_handle_y_rotation = 0;
	float hand_on_handle_z_rotation = 0;
	if (hand == XRT_HAND_LEFT) {
		hand_on_handle_z_rotation = DEG_TO_RAD(-75);
	} else if (hand == XRT_HAND_RIGHT) {
		hand_on_handle_z_rotation = DEG_TO_RAD(75);
	}


	struct xrt_quat hand_rotation_y = XRT_QUAT_IDENTITY;
	math_quat_from_angle_vector(hand_on_handle_y_rotation, &y, &hand_rotation_y);

	struct xrt_quat hand_rotation_z = XRT_QUAT_IDENTITY;
	math_quat_from_angle_vector(hand_on_handle_z_rotation, &negative_z, &hand_rotation_z);

	struct xrt_quat hand_rotation_x = XRT_QUAT_IDENTITY;
	math_quat_from_angle_vector(hand_on_handle_x_rotation, &x, &hand_rotation_x);

	struct xrt_quat hand_rotation;
	math_quat_rotate(&hand_rotation_x, &hand_rotation_z, &hand_rotation);

	struct xrt_pose hand_on_handle_pose = {.orientation = hand_rotation, .position = offset_position};

	printf("%d %f %f %f  %f %f %f %f\n", hand, hand_on_handle_pose.position.x, hand_on_handle_pose.position.y, hand_on_handle_pose.position.z, hand_on_handle_pose.orientation.w,       \
	       hand_on_handle_pose.orientation.x, hand_on_handle_pose.orientation.y, hand_on_handle_pose.orientation.z);

	*offset = hand_on_handle_pose;
#else
	switch (hand) {
	case XRT_HAND_LEFT: {
		*offset = (struct xrt_pose){
		    .orientation = {.w = 0.641836, .x = -0.466321, .y = 0.357821, .z = 0.492498},
		    .position = offset_position,
		};
		break;
	}

	case XRT_HAND_RIGHT: {
		*offset = (struct xrt_pose){
		    .orientation = {.w = 0.641836, .x = -0.466321, .y = -0.357821, .z = -0.492498},
		    .position = offset_position,
		};
		break;
	}
	}

#endif
}

static void
vive_poses_get_index_offset_pose(const enum xrt_input_name input_name,
                                 const enum xrt_device_type device_type,
                                 struct xrt_pose *out_offset_pose)
{
	switch (input_name) {
	case XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT:
		vive_poses_get_index_hand_offset_pose(XRT_HAND_RIGHT, out_offset_pose);
		return;
	case XRT_INPUT_GENERIC_HAND_TRACKING_LEFT:
		vive_poses_get_index_hand_offset_pose(XRT_HAND_LEFT, out_offset_pose);
		return;
	default: break; // Go to code below.
	}

	// Note that XRT_INPUT_GENERIC_TRACKER_POSE goes down this path
	struct xrt_vec3 transform_position = XRT_VEC3_ZERO;
	struct xrt_vec3 transform_rotation = XRT_VEC3_ZERO;
	vive_poses_get_index_offset_euler(input_name, device_type, &transform_position, &transform_rotation);
	math_quat_from_euler_angles(&transform_rotation, &out_offset_pose->orientation);
	out_offset_pose->position = transform_position;
}

void
vive_poses_get_pose_offset(enum xrt_device_name device_name,
                           enum xrt_device_type device_type,
                           enum xrt_input_name input_name,
                           struct xrt_pose *out_offset_pose)
{

	switch (device_name) {
	case XRT_DEVICE_INDEX_CONTROLLER:
		vive_poses_get_index_offset_pose(input_name, device_type, out_offset_pose);
		break;
	default: *out_offset_pose = (struct xrt_pose)XRT_POSE_IDENTITY;
	}
}
