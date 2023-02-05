// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vive poses implementation
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_vive
 */

#include "vive_poses.h"

#include "math/m_mathinclude.h"
#include "math/m_api.h"

#define DEG_TO_RAD(DEG) (DEG * M_PI / 180.)

void
vive_poses_apply_right_transform(struct xrt_vec3 *out_transform_position, struct xrt_vec3 *out_transform_rotation)
{
	out_transform_rotation->y *= -1.f;
	out_transform_rotation->z *= -1.f;

	out_transform_position->x *= -1.f;
}

void
vive_poses_get_index_offset_transforms(enum xrt_input_name input_name,
                                       enum xrt_device_type device_type,
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



void
vive_poses_get_pose_offset(enum xrt_device_name device_name,
                           enum xrt_device_type device_type,
                           enum xrt_input_name input_name,
                           struct xrt_pose *out_offset_pose)
{
	struct xrt_vec3 transform_position = XRT_VEC3_ZERO;
	struct xrt_vec3 transform_rotation = XRT_VEC3_ZERO;

	switch (device_name) {
	case XRT_DEVICE_INDEX_CONTROLLER:
		vive_poses_get_index_offset_transforms(input_name, device_type, &transform_position,
		                                       &transform_rotation);
		break;
	default: break;
	}

	math_quat_from_euler_angles(&transform_rotation, &out_offset_pose->orientation);
	out_offset_pose->position = transform_position;
}
