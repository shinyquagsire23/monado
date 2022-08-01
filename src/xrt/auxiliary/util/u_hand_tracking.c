// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Hand Tracking API interface.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Daniel Willmott <web@dan-w.com>
 * @author Nick Klingensmith <programmerpichu@gmail.com>
 * @ingroup aux_tracking
 */

#include "u_hand_tracking.h"

#include "math/m_mathinclude.h"
#include "math/m_api.h"
#include "math/m_space.h"
#include "math/m_vec3.h"
#include "math/m_api.h"

#include "util/u_time.h"


#define DEG_TO_RAD(DEG) (DEG * M_PI / 180.)

bool
u_hand_joint_is_metacarpal(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_LITTLE_METACARPAL || joint == XRT_HAND_JOINT_RING_METACARPAL ||
	       joint == XRT_HAND_JOINT_MIDDLE_METACARPAL || joint == XRT_HAND_JOINT_INDEX_METACARPAL ||
	       joint == XRT_HAND_JOINT_THUMB_METACARPAL;
}

bool
u_hand_joint_is_proximal(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_LITTLE_PROXIMAL || joint == XRT_HAND_JOINT_RING_PROXIMAL ||
	       joint == XRT_HAND_JOINT_MIDDLE_PROXIMAL || joint == XRT_HAND_JOINT_INDEX_PROXIMAL ||
	       joint == XRT_HAND_JOINT_THUMB_PROXIMAL;
}

bool
u_hand_joint_is_intermediate(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_LITTLE_INTERMEDIATE || joint == XRT_HAND_JOINT_RING_INTERMEDIATE ||
	       joint == XRT_HAND_JOINT_MIDDLE_INTERMEDIATE || joint == XRT_HAND_JOINT_INDEX_INTERMEDIATE;
}

bool
u_hand_joint_is_distal(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_LITTLE_DISTAL || joint == XRT_HAND_JOINT_RING_DISTAL ||
	       joint == XRT_HAND_JOINT_MIDDLE_DISTAL || joint == XRT_HAND_JOINT_INDEX_DISTAL ||
	       joint == XRT_HAND_JOINT_THUMB_DISTAL;
}

bool
u_hand_joint_is_tip(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_LITTLE_TIP || joint == XRT_HAND_JOINT_RING_TIP ||
	       joint == XRT_HAND_JOINT_MIDDLE_TIP || joint == XRT_HAND_JOINT_INDEX_TIP ||
	       joint == XRT_HAND_JOINT_THUMB_TIP;
}

bool
u_hand_joint_is_thumb(enum xrt_hand_joint joint)
{
	return joint == XRT_HAND_JOINT_THUMB_METACARPAL || joint == XRT_HAND_JOINT_THUMB_PROXIMAL ||
	       joint == XRT_HAND_JOINT_THUMB_DISTAL || joint == XRT_HAND_JOINT_THUMB_TIP;
}

void
u_hand_joints_apply_joint_width(struct xrt_hand_joint_set *set)
{
	// Thanks to Nick Klingensmith for this idea
	struct xrt_hand_joint_value *gr = set->values.hand_joint_set_default;

	static const float finger_joint_size[5] = {0.022f, 0.021f, 0.022f, 0.021f, 0.02f};
	static const float hand_finger_size[5] = {1.0f, 1.0f, 0.83f, 0.75f};

	static const float thumb_size[4] = {0.016f, 0.014f, 0.012f, 0.012f};
	float mul = 1.0f;


	for (int i = XRT_HAND_JOINT_THUMB_METACARPAL; i <= XRT_HAND_JOINT_THUMB_TIP; i++) {
		int j = i - XRT_HAND_JOINT_THUMB_METACARPAL;
		gr[i].radius = thumb_size[j] * mul;
	}

	for (int finger = 0; finger < 4; finger++) {
		for (int joint = 0; joint < 5; joint++) {
			int set_idx = finger * 5 + joint + XRT_HAND_JOINT_INDEX_METACARPAL;
			float val = finger_joint_size[joint] * hand_finger_size[finger] * .5 * mul;
			gr[set_idx].radius = val;
		}
	}
	// The radius of each joint is the distance from the joint to the skin in meters. -OpenXR spec.
	set->values.hand_joint_set_default[XRT_HAND_JOINT_PALM].radius =
	    .032f * .5f; // Measured my palm thickness with calipers
	set->values.hand_joint_set_default[XRT_HAND_JOINT_WRIST].radius =
	    .040f * .5f; // Measured my wrist thickness with calipers
}

void
u_hand_joints_offset_valve_index_controller(enum xrt_hand hand,
                                            const struct xrt_vec3 *static_offset,
                                            struct xrt_pose *offset)
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

	struct xrt_pose hand_on_handle_pose = {.orientation = hand_rotation, .position = *static_offset};

	*offset = hand_on_handle_pose;
}
