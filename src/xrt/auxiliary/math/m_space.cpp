// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for manipulating a @ref xrt_relation_chain struct.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */

#include "util/u_misc.h"

#include "math/m_api.h"
#include "math/m_vec2.h"
#include "math/m_vec3.h"
#include "math/m_space.h"

#include <stdio.h>
#include <assert.h>


/*
 *
 * Dump functions.
 *
 */

static void
dump_relation(const struct xrt_space_relation *r)
{
	fprintf(stderr, "%04x", r->relation_flags);

	if (r->relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) {
		fprintf(stderr, " P{%f %f %f}", r->pose.position.x, r->pose.position.y, r->pose.position.z);
	}

	if (r->relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) {
		fprintf(stderr, " O{%f %f %f %f}", r->pose.orientation.x, r->pose.orientation.y, r->pose.orientation.z,
		        r->pose.orientation.w);
	}

	if (r->relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) {
		fprintf(stderr, " LV{%f %f %f}", r->linear_velocity.x, r->linear_velocity.y, r->linear_velocity.z);
	}

	if (r->relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) {
		fprintf(stderr, " AV{%f %f %f}", r->angular_velocity.x, r->angular_velocity.y, r->angular_velocity.z);
	}

	fprintf(stderr, "\n");
}

static void
dump_chain(const struct xrt_relation_chain *xrc)
{
	fprintf(stderr, "%s %u\n", __func__, xrc->step_count);
	for (uint32_t i = 0; i < xrc->step_count; i++) {
		const struct xrt_space_relation *r = &xrc->steps[i];
		fprintf(stderr, "\t%2u: ", i);
		dump_relation(r);
	}
}


/*
 *
 * Helper functions.
 *
 */

static bool
has_step_with_no_pose(const struct xrt_relation_chain *xrc)
{
	const enum xrt_space_relation_flags pose_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_VALID_BIT);

	for (uint32_t i = 0; i < xrc->step_count; i++) {
		const struct xrt_space_relation *r = &xrc->steps[i];
		if ((r->relation_flags & pose_flags) == 0) {
			return true;
		}
	}

	return false;
}

struct flags
{
	unsigned int has_orientation : 1;
	unsigned int has_position : 1;
	unsigned int has_linear_velocity : 1;
	unsigned int has_angular_velocity : 1;
	unsigned int has_tracked_orientation : 1;
	unsigned int has_tracked_position : 1;
};

flags
get_flags(const struct xrt_space_relation *r)
{
	// clang-format off
	flags flags = {};
	flags.has_orientation = (r->relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) != 0;
	flags.has_position = (r->relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) != 0;
	flags.has_linear_velocity = (r->relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) != 0;
	flags.has_angular_velocity = (r->relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) != 0;
	flags.has_tracked_orientation = (r->relation_flags & XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT) != 0;
	flags.has_tracked_position = (r->relation_flags & XRT_SPACE_RELATION_POSITION_TRACKED_BIT) != 0;
	// clang-format on

	return flags;
}

static void
make_valid_pose(flags flags, const struct xrt_pose *in_pose, struct xrt_pose *out_pose)
{
	if (flags.has_orientation) {
		out_pose->orientation = in_pose->orientation;
	} else {
		out_pose->orientation = XRT_QUAT_IDENTITY;
	}

	if (flags.has_position) {
		out_pose->position = in_pose->position;
	} else {
		out_pose->position = XRT_VEC3_ZERO;
	}
}

static void
apply_relation(const struct xrt_space_relation *a,
               const struct xrt_space_relation *b,
               struct xrt_space_relation *out_relation)
{
	flags af = get_flags(a);
	flags bf = get_flags(b);

	flags nf = {};
	struct xrt_pose pose = XRT_POSE_IDENTITY;
	struct xrt_vec3 linear_velocity = XRT_VEC3_ZERO;
	struct xrt_vec3 angular_velocity = XRT_VEC3_ZERO;


	/*
	 * Pose.
	 */

	struct xrt_pose body_pose = XRT_POSE_IDENTITY; // aka valid_a_pose
	struct xrt_pose base_pose = XRT_POSE_IDENTITY; // aka valid_b_pose

	// Only valid poses handled in chain. Flags are determined later.
	make_valid_pose(af, &a->pose, &body_pose);
	make_valid_pose(bf, &b->pose, &base_pose);

	// Pose will be undefined if we don't have at least rotation.
	math_pose_transform(&base_pose, &body_pose, &pose);


	/*
	 * Linear velocity.
	 */

	if (af.has_linear_velocity) {
		nf.has_linear_velocity = true;
		struct xrt_vec3 tmp = XRT_VEC3_ZERO;

		math_quat_rotate_vec3(&base_pose.orientation, // Base rotation
		                      &a->linear_velocity,    // In base space
		                      &tmp);                  // Output

		linear_velocity += tmp;
	}

	if (bf.has_linear_velocity) {
		nf.has_linear_velocity = true;
		linear_velocity += b->linear_velocity;
	}


	/*
	 * Angular velocity.
	 */

	if (af.has_angular_velocity) {
		nf.has_angular_velocity = true;
		struct xrt_vec3 tmp = XRT_VEC3_ZERO;

		math_quat_rotate_derivative(&base_pose.orientation, // Base rotation
		                            &a->angular_velocity,   // In base space
		                            &tmp);                  // Output

		angular_velocity += tmp;
	}

	if (bf.has_angular_velocity) {
		nf.has_angular_velocity = true;
		nf.has_linear_velocity = true;
		angular_velocity += b->angular_velocity;

		// handle tangential velocity AKA "lever arm" effect on velocity:
		// an angular velocity at the origin produces a linear velocity everywhere else
		struct xrt_vec3 rotated_position = XRT_VEC3_ZERO;
		struct xrt_vec3 position = XRT_VEC3_ZERO;
		struct xrt_quat orientation = XRT_QUAT_IDENTITY;
		struct xrt_vec3 tangental_velocity = XRT_VEC3_ZERO;

		position = body_pose.position;       // In the base space
		orientation = base_pose.orientation; // Base space

		math_quat_rotate_vec3(&orientation,       // Rotation
		                      &position,          // Vector
		                      &rotated_position); // Result

		math_vec3_cross(&b->angular_velocity, // A
		                &rotated_position,    // B
		                &tangental_velocity); // Result

		linear_velocity += tangental_velocity;
	}


	/*
	 * Flags.
	 */

	int new_flags = 0;

	/*
	 * Make sure to not drop a space relation, even if only either position
	 * or orintation is valid. We should not be getting here if neither
	 * position and orintation is valid.
	 *
	 * When position is valid, always set orientation valid to "upgrade"
	 * poses with valid position but invalid orientation to fully valid
	 * pose using identity quat, @see make_valid_pose.
	 *
	 * When orientation is valid, always set position valid to "upgrade"
	 * poses with valid orientation but invalid position to fully valid
	 * pose using identity vec3, @see make_valid_pose.
	 */
	assert(af.has_position || af.has_orientation);
	assert(bf.has_position || bf.has_orientation);

	new_flags |= XRT_SPACE_RELATION_POSITION_VALID_BIT;
	new_flags |= XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;

	//! @todo combining these flags with OR is probably okay for now
	if (af.has_tracked_position || bf.has_tracked_position) {
		new_flags |= XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
	}
	if (af.has_tracked_orientation || bf.has_tracked_orientation) {
		new_flags |= XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;
	}
	if (nf.has_linear_velocity) {
		new_flags |= XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT;
	}
	if (nf.has_angular_velocity) {
		new_flags |= XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT;
	}


	/*
	 * Write everything out.
	 */

	struct xrt_space_relation tmp = {};
	tmp.relation_flags = (enum xrt_space_relation_flags)new_flags;
	tmp.pose = pose;
	tmp.linear_velocity = linear_velocity;
	tmp.angular_velocity = angular_velocity;

	*out_relation = tmp;
}


/*
 *
 * Exported functions.
 *
 */

extern "C" void
m_relation_chain_resolve(const struct xrt_relation_chain *xrc, struct xrt_space_relation *out_relation)
{
	if (xrc->step_count == 0 || has_step_with_no_pose(xrc)) {
		*out_relation = XRT_SPACE_RELATION_ZERO;
		return;
	}

	struct xrt_space_relation r = xrc->steps[0];
	for (uint32_t i = 1; i < xrc->step_count; i++) {
		apply_relation(&r, &xrc->steps[i], &r);
	}

#if 0
	dump_chain(xrc);
	fprintf(stderr, "\tRR: ");
	dump_relation(&r);
#else
	(void)&dump_chain;
#endif

	// Ensure no errors have crept in.
	math_quat_normalize(&r.pose.orientation);

	*out_relation = r;
}

extern "C" void
m_space_relation_invert(struct xrt_space_relation *relation, struct xrt_space_relation *out_relation)
{
	assert(relation != NULL);
	assert(out_relation != NULL);

	out_relation->relation_flags = relation->relation_flags;
	math_pose_invert(&relation->pose, &out_relation->pose);
	out_relation->linear_velocity = m_vec3_mul_scalar(relation->linear_velocity, -1);
	out_relation->angular_velocity = m_vec3_mul_scalar(relation->angular_velocity, -1);
}

extern "C" void
m_space_relation_interpolate(struct xrt_space_relation *a,
                             struct xrt_space_relation *b,
                             float t,
                             enum xrt_space_relation_flags flags,
                             struct xrt_space_relation *out_relation)
{
	out_relation->relation_flags = flags;

	if (flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) {
		math_quat_slerp(&a->pose.orientation, &b->pose.orientation, t, &out_relation->pose.orientation);
	}
	if (flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) {
		out_relation->pose.position = m_vec3_lerp(a->pose.position, b->pose.position, t);
	}
	if (flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) {
		out_relation->linear_velocity = m_vec3_lerp(a->linear_velocity, b->linear_velocity, t);
	}
	if (flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) {
		out_relation->angular_velocity = m_vec3_lerp(a->angular_velocity, b->angular_velocity, t);
	}
}
