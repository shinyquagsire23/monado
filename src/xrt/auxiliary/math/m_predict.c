// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple function to predict a new pose from a given pose.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */

#include "m_api.h"
#include "m_vec3.h"
#include "m_predict.h"


static void
do_orientation(const struct xrt_space_relation *rel,
               enum xrt_space_relation_flags flags,
               double delta_s,
               struct xrt_space_relation *out_rel)
{
	if (delta_s == 0) {
		out_rel->pose.orientation = rel->pose.orientation;
		out_rel->angular_velocity = rel->angular_velocity;
		return;
	}

	struct xrt_vec3 accum = {0};
	bool valid_orientation = (flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0;
	bool valid_angular_velocity = (flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) != 0;

	if (valid_angular_velocity) {
		// angular velocity needs to be in body space for prediction
		struct xrt_vec3 ang_vel_body_space;

		struct xrt_quat orientation_inv;
		math_quat_invert(&rel->pose.orientation, &orientation_inv);

		math_quat_rotate_derivative(&orientation_inv, &rel->angular_velocity, &ang_vel_body_space);

		accum.x += ang_vel_body_space.x;
		accum.y += ang_vel_body_space.y;
		accum.z += ang_vel_body_space.z;
	}

	// We don't want the angular acceleration, it's way to noisy.
#if 0
	if (valid_angular_acceleration) {
		accum.x += delta_s / 2 * rel->angular_acceleration.x;
		accum.y += delta_s / 2 * rel->angular_acceleration.y;
		accum.z += delta_s / 2 * rel->angular_acceleration.z;
	}
#endif

	if (valid_orientation) {
		math_quat_integrate_velocity(&rel->pose.orientation,      // Old orientation
		                             &accum,                      // Angular velocity
		                             delta_s,                     // Delta in seconds
		                             &out_rel->pose.orientation); // Result
	}

	// We use everything we integrated in as the new angular_velocity.
	if (valid_angular_velocity) {
		// angular velocity is returned in base space.
		// use the predicted orientation for this calculation.
		struct xrt_vec3 predicted_ang_vel_base_space;
		math_quat_rotate_derivative(&out_rel->pose.orientation, &accum, &predicted_ang_vel_base_space);

		out_rel->angular_velocity = predicted_ang_vel_base_space;
	}
}

static void
do_position(const struct xrt_space_relation *rel,
            enum xrt_space_relation_flags flags,
            double delta_s,
            struct xrt_space_relation *out_rel)
{
	if (delta_s == 0) {
		out_rel->pose.position = rel->pose.position;
		out_rel->linear_velocity = rel->linear_velocity;
		return;
	}

	struct xrt_vec3 accum = {0};
	bool valid_position = (flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) != 0;
	bool valid_linear_velocity = (flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) != 0;

	if (valid_linear_velocity) {
		accum.x += rel->linear_velocity.x;
		accum.y += rel->linear_velocity.y;
		accum.z += rel->linear_velocity.z;
	}

	if (valid_position) {
		out_rel->pose.position = m_vec3_add(rel->pose.position, m_vec3_mul_scalar(accum, delta_s));
	}

	// We use the new linear velocity with the acceleration integrated.
	if (valid_linear_velocity) {
		out_rel->linear_velocity = accum;
	}
}

void
m_predict_relation(const struct xrt_space_relation *rel, double delta_s, struct xrt_space_relation *out_rel)
{
	enum xrt_space_relation_flags flags = rel->relation_flags;

	do_orientation(rel, flags, delta_s, out_rel);
	do_position(rel, flags, delta_s, out_rel);

	out_rel->relation_flags = flags;
}
