// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Base implementations for math library.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#include "math/m_api.h"
#include "math/m_eigen_interop.hpp"

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <assert.h>


/*
 *
 * Copy helpers.
 *
 */

static inline Eigen::Quaternionf
copy(const struct xrt_quat &q)
{
	// Eigen constructor order is different from XRT, OpenHMD and OpenXR!
	//  Eigen: `float w, x, y, z`.
	// OpenXR: `float x, y, z, w`.
	return Eigen::Quaternionf(q.w, q.x, q.y, q.z);
}

static inline Eigen::Quaternionf
copy(const struct xrt_quat *q)
{
	return copy(*q);
}

static inline Eigen::Vector3f
copy(const struct xrt_vec3 &v)
{
	return Eigen::Vector3f(v.x, v.y, v.z);
}

static inline Eigen::Vector3f
copy(const struct xrt_vec3 *v)
{
	return copy(*v);
}

static inline Eigen::Matrix4f
copy(const struct xrt_matrix_4x4 *m)
{
	Eigen::Matrix4f res;
	// clang-format off
	res << m->v[0], m->v[4], m->v[8],  m->v[12],
	       m->v[1], m->v[5], m->v[9],  m->v[13],
	       m->v[2], m->v[6], m->v[10], m->v[14],
	       m->v[3], m->v[7], m->v[11], m->v[15];
	// clang-format on
	return res;
}

/*
 *
 * Exported vector functions.
 *
 */

extern "C" bool
math_vec3_validate(const struct xrt_vec3 *vec3)
{
	assert(vec3 != NULL);

	return map_vec3(*vec3).allFinite();
}

extern "C" void
math_vec3_accum(const struct xrt_vec3 *additional, struct xrt_vec3 *inAndOut)
{
	assert(additional != NULL);
	assert(inAndOut != NULL);

	map_vec3(*inAndOut) += map_vec3(*additional);
}

extern "C" void
math_vec3_cross(const struct xrt_vec3 *l,
                const struct xrt_vec3 *r,
                struct xrt_vec3 *result)
{
	map_vec3(*result) = map_vec3(*l).cross(map_vec3(*r));
}

extern "C" void
math_vec3_normalize(struct xrt_vec3 *in)
{
	map_vec3(*in) = map_vec3(*in).normalized();
}

/*
 *
 * Exported quaternion functions.
 *
 */

extern "C" void
math_quat_from_angle_vector(float angle_rads,
                            const struct xrt_vec3 *vector,
                            struct xrt_quat *result)
{
	map_quat(*result) = Eigen::AngleAxisf(angle_rads, copy(vector));
}

extern "C" void
math_quat_from_matrix_3x3(const struct xrt_matrix_3x3 *mat,
                          struct xrt_quat *result)
{
	Eigen::Matrix3f m;
	m << mat->v[0], mat->v[1], mat->v[2], mat->v[3], mat->v[4], mat->v[5],
	    mat->v[6], mat->v[7], mat->v[8];

	Eigen::Quaternionf q(m);
	map_quat(*result) = q;
}

extern "C" void
math_quat_from_plus_x_z(const struct xrt_vec3 *plus_x,
                        const struct xrt_vec3 *plus_z,
                        struct xrt_quat *result)
{
	xrt_vec3 plus_y;
	math_vec3_cross(plus_z, plus_x, &plus_y);

	xrt_matrix_3x3 m = {{
	    plus_x->x,
	    plus_y.x,
	    plus_z->x,
	    plus_x->y,
	    plus_y.y,
	    plus_z->y,
	    plus_x->z,
	    plus_y.z,
	    plus_z->z,
	}};

	math_quat_from_matrix_3x3(&m, result);
}

extern "C" bool
math_quat_validate(const struct xrt_quat *quat)
{
	assert(quat != NULL);
	auto rot = copy(*quat);

	const float FLOAT_EPSILON = Eigen::NumTraits<float>::epsilon();
	/*
	 * This was originally squaredNorm, but that could result in a norm
	 * value that was further from 1.0f then FLOAT_EPSILON (two).
	 *
	 * Our tracking system would produce such orientations and looping those
	 * back into say a quad layer would cause this to fail. And even
	 * normalizing the quat would not fix this as normalizations uses
	 * non-squared "length" which does fall into the range and doesn't
	 * change the elements of the quat.
	 */
	auto norm = rot.norm();
	if (norm > 1.0f + FLOAT_EPSILON || norm < 1.0f - FLOAT_EPSILON) {
		return false;
	}

	// Technically not yet a required check, but easier to stop problems
	// now than once denormalized numbers pollute the rest of our state.
	// see https://gitlab.khronos.org/openxr/openxr/issues/922
	if (!rot.coeffs().allFinite()) {
		return false;
	}

	return true;
}

extern "C" void
math_quat_invert(const struct xrt_quat *quat, struct xrt_quat *out_quat)
{
	map_quat(*out_quat) = map_quat(*quat).conjugate();
}

extern "C" void
math_quat_normalize(struct xrt_quat *inout)
{
	assert(inout != NULL);
	map_quat(*inout).normalize();
}

extern "C" bool
math_quat_ensure_normalized(struct xrt_quat *inout)
{
	assert(inout != NULL);

	if (math_quat_validate(inout))
		return true;

	const float FLOAT_EPSILON = Eigen::NumTraits<float>::epsilon();
	const float TOLERANCE = FLOAT_EPSILON * 5;

	auto rot = copy(*inout);
	auto norm = rot.norm();
	if (norm > 1.0f + TOLERANCE || norm < 1.0f - TOLERANCE) {
		return false;
	}

	map_quat(*inout).normalize();
	return true;
}


extern "C" void
math_quat_rotate(const struct xrt_quat *left,
                 const struct xrt_quat *right,
                 struct xrt_quat *result)
{
	assert(left != NULL);
	assert(right != NULL);
	assert(result != NULL);

	auto l = copy(left);
	auto r = copy(right);

	auto q = l * r;

	map_quat(*result) = q;
}

extern "C" void
math_quat_rotate_vec3(const struct xrt_quat *left,
                      const struct xrt_vec3 *right,
                      struct xrt_vec3 *result)
{
	assert(left != NULL);
	assert(right != NULL);
	assert(result != NULL);

	auto l = copy(left);
	auto r = copy(right);

	auto v = l * r;

	map_vec3(*result) = v;
}


/*
 *
 * Exported matrix functions.
 *
 */

extern "C" void
math_matrix_3x3_transform_vec3(const struct xrt_matrix_3x3 *left,
                               const struct xrt_vec3 *right,
                               struct xrt_vec3 *result)
{
	Eigen::Matrix3f m;
	m << left->v[0], left->v[1], left->v[2], // 1
	    left->v[3], left->v[4], left->v[5],  // 2
	    left->v[6], left->v[7], left->v[8];  // 3

	map_vec3(*result) = m * copy(right);
}


void
math_matrix_4x4_identity(struct xrt_matrix_4x4 *result)
{
	map_matrix_4x4(*result) = Eigen::Matrix4f::Identity();
}

void
math_matrix_4x4_multiply(const struct xrt_matrix_4x4 *left,
                         const struct xrt_matrix_4x4 *right,
                         struct xrt_matrix_4x4 *result)
{
	map_matrix_4x4(*result) = copy(left) * copy(right);
}

void
math_matrix_4x4_view_from_pose(const struct xrt_pose *pose,
                               struct xrt_matrix_4x4 *result)
{
	Eigen::Vector3f position = copy(&pose->position);
	Eigen::Quaternionf orientation = copy(&pose->orientation);

	Eigen::Translation3f translation(position);
	Eigen::Affine3f transformation = translation * orientation;

	map_matrix_4x4(*result) = transformation.matrix().inverse();
}

void
math_matrix_4x4_model(const struct xrt_pose *pose,
                      const struct xrt_vec3 *size,
                      struct xrt_matrix_4x4 *result)
{
	Eigen::Vector3f position = copy(&pose->position);
	Eigen::Quaternionf orientation = copy(&pose->orientation);

	auto scale = Eigen::Scaling(size->x, size->y, size->z);

	Eigen::Translation3f translation(position);
	Eigen::Affine3f transformation = translation * orientation * scale;

	map_matrix_4x4(*result) = transformation.matrix();
}

/*
 *
 * Exported pose functions.
 *
 */

extern "C" bool
math_pose_validate(const struct xrt_pose *pose)
{
	assert(pose != NULL);

	return math_vec3_validate(&pose->position) &&
	       math_quat_validate(&pose->orientation);
}

extern "C" void
math_pose_invert(const struct xrt_pose *pose, struct xrt_pose *outPose)
{
	assert(pose != NULL);
	assert(outPose != NULL);

	// Store results to temporary locals so we can do this "in-place"
	// (pose == outPose) if desired. Pure copies here.
	Eigen::Vector3f newPosition = position(*pose);
	Eigen::Quaternionf newOrientation = orientation(*pose);

	// Conjugate legal here since pose must be normalized/unit length.
	newOrientation = newOrientation.conjugate();
	// Use the newly inverted rotation, to rotate position.
	newPosition = -(newOrientation * newPosition);

	position(*outPose) = newPosition;
	orientation(*outPose) = newOrientation;
}

/*!
 * Return the result of transforming a point by a pose/transform.
 */
static inline Eigen::Vector3f
transform_point(const xrt_pose &transform, const xrt_vec3 &point)
{
	return orientation(transform) * map_vec3(point) + position(transform);
}

/*!
 * Return the result of transforming a pose by a pose/transform.
 */
static inline xrt_pose
transform_pose(const xrt_pose &transform, const xrt_pose &pose)
{
	xrt_pose ret;
	position(ret) = transform_point(transform, pose.position);
	orientation(ret) = orientation(transform) * orientation(pose);
	return ret;
}

extern "C" void
math_pose_transform(const struct xrt_pose *transform,
                    const struct xrt_pose *pose,
                    struct xrt_pose *outPose)
{
	assert(pose != NULL);
	assert(transform != NULL);
	assert(outPose != NULL);

	xrt_pose newPose = transform_pose(*transform, *pose);
	memcpy(outPose, &newPose, sizeof(xrt_pose));
}

extern "C" void
math_pose_transform_point(const struct xrt_pose *transform,
                          const struct xrt_vec3 *point,
                          struct xrt_vec3 *out_point)
{
	assert(transform != NULL);
	assert(point != NULL);
	assert(out_point != NULL);

	map_vec3(*out_point) = transform_point(*transform, *point);
}

extern "C" void
math_pose_openxr_locate(const struct xrt_pose *space_pose,
                        const struct xrt_pose *relative_pose,
                        const struct xrt_pose *base_space_pose,
                        struct xrt_pose *result)
{
	assert(space_pose != NULL);
	assert(relative_pose != NULL);
	assert(base_space_pose != NULL);
	assert(result != NULL);

	// Compilers are slightly better optimizing
	// if we copy the arguments in one go.
	const auto bsp = *base_space_pose;
	const auto rel = *relative_pose;
	const auto spc = *space_pose;
	struct xrt_pose pose;

	// Apply the invert of the base space to identity.
	math_pose_invert(&bsp, &pose);

	// Apply the pure pose from the space relation.
	math_pose_transform(&pose, &rel, &pose);

	// Apply the space pose.
	math_pose_transform(&pose, &spc, &pose);

	*result = pose;
}

/*!
 * Return the result of rotating a derivative vector by a matrix.
 *
 * This is a differential transform.
 */
static inline Eigen::Vector3f
rotate_deriv(Eigen::Matrix3f const &rotation,
             const xrt_vec3 &derivativeVector,
             Eigen::Matrix3f const &rotationInverse)
{
	return ((rotation * map_vec3(derivativeVector)).transpose() *
	        rotationInverse)
	    .transpose();
}

#ifndef XRT_DOXYGEN

#define MAKE_REL_FLAG_CHECK(NAME, MASK)                                        \
	static inline bool NAME(xrt_space_relation_flags flags)                \
	{                                                                      \
		return ((flags & (MASK)) != 0);                                \
	}

MAKE_REL_FLAG_CHECK(has_some_pose_component,
                    XRT_SPACE_RELATION_POSITION_VALID_BIT |
                        XRT_SPACE_RELATION_ORIENTATION_VALID_BIT)
MAKE_REL_FLAG_CHECK(has_position, XRT_SPACE_RELATION_POSITION_VALID_BIT)
MAKE_REL_FLAG_CHECK(has_orientation, XRT_SPACE_RELATION_ORIENTATION_VALID_BIT)
MAKE_REL_FLAG_CHECK(has_lin_vel, XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT)
MAKE_REL_FLAG_CHECK(has_ang_vel, XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT)
MAKE_REL_FLAG_CHECK(has_lin_acc,
                    XRT_SPACE_RELATION_LINEAR_ACCELERATION_VALID_BIT)
MAKE_REL_FLAG_CHECK(has_ang_acc,
                    XRT_SPACE_RELATION_ANGULAR_ACCELERATION_VALID_BIT)
MAKE_REL_FLAG_CHECK(has_some_derivative,
                    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT |
                        XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT |
                        XRT_SPACE_RELATION_LINEAR_ACCELERATION_VALID_BIT |
                        XRT_SPACE_RELATION_ANGULAR_ACCELERATION_VALID_BIT)

#undef MAKE_REL_FLAG_CHECK

#endif // !XRT_DOXYGEN

enum accumulate_pose_flags
{
	OFFSET,
	LEGACY,
};

/*!
 * Apply a transform to a space relation.
 */
static inline void
transform_accumulate_pose(const xrt_pose &transform,
                          xrt_space_relation &relation,
                          enum accumulate_pose_flags accum_flags,
                          bool do_translation = true,
                          bool do_rotation = true)
{
	assert(do_translation || do_rotation);

	// Save the quat in case we are self-transforming.
	Eigen::Quaternionf quat = orientation(transform);

	auto flags = relation.relation_flags;
	// so code looks similar
	auto in_out_relation = &relation;

	// transform (rotate and translate) the pose, if applicable.
	if (has_some_pose_component(flags)) {
		// Zero out transform parts we don't want to use,
		// because math_pose_transform doesn't take flags.
		xrt_pose transform_copy = transform;
		if (!do_translation) {
			position(transform_copy) = Eigen::Vector3f::Zero();
		}
		if (!do_rotation) {
			orientation(transform_copy) =
			    Eigen::Quaternionf::Identity();
		}

		//! @todo This is just a big hack.
		if (accum_flags == OFFSET) {
			math_pose_transform(&transform, &in_out_relation->pose,
			                    &in_out_relation->pose);
		} else {
			math_pose_transform(&in_out_relation->pose, &transform,
			                    &in_out_relation->pose);
		}
	}

	if (do_rotation && has_some_derivative(flags)) {

		// prepare matrices required for rotating derivatives from the
		// saved quat.
		Eigen::Matrix3f rot = quat.toRotationMatrix();
		Eigen::Matrix3f rotInverse = rot.inverse();

		// Rotate derivatives, if applicable.
		if (has_lin_vel(flags)) {
			map_vec3(in_out_relation->linear_velocity) =
			    rotate_deriv(rot, in_out_relation->linear_velocity,
			                 rotInverse);
		}

		if (has_ang_vel(flags)) {
			map_vec3(in_out_relation->angular_velocity) =
			    rotate_deriv(rot, in_out_relation->angular_velocity,
			                 rotInverse);
		}

		if (has_lin_acc(flags)) {
			map_vec3(in_out_relation->linear_acceleration) =
			    rotate_deriv(rot,
			                 in_out_relation->linear_acceleration,
			                 rotInverse);
		}

		if (has_ang_acc(flags)) {
			map_vec3(in_out_relation->angular_acceleration) =
			    rotate_deriv(rot,
			                 in_out_relation->angular_acceleration,
			                 rotInverse);
		}
	}
}

static const struct xrt_space_relation BLANK_RELATION = {
    XRT_SPACE_RELATION_BITMASK_ALL,
    {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
    {0, 0, 0},
    {0, 0, 0},
    {0, 0, 0},
    {0, 0, 0},
};

extern "C" void
math_relation_reset(struct xrt_space_relation *out)
{
	*out = BLANK_RELATION;
}

extern "C" void
math_relation_apply_offset(const struct xrt_pose *offset,
                           struct xrt_space_relation *in_out_relation)
{
	assert(offset != nullptr);
	assert(in_out_relation != nullptr);

	// No modifying the validity flags here.
	transform_accumulate_pose(*offset, *in_out_relation, OFFSET);
}

void
accumulate_transform(const struct xrt_pose *transform,
                     struct xrt_space_relation *in_out_relation)
{
	assert(transform != nullptr);
	assert(in_out_relation != nullptr);

	// No modifying the validity flags here.
	transform_accumulate_pose(*transform, *in_out_relation, LEGACY);
}

extern "C" void
math_relation_accumulate_relation(
    const struct xrt_space_relation *additional_relation,
    struct xrt_space_relation *in_out_relation)
{
	assert(additional_relation != NULL);
	assert(in_out_relation != NULL);

	// Update the flags.
	xrt_space_relation_flags flags = (enum xrt_space_relation_flags)(
	    in_out_relation->relation_flags &
	    additional_relation->relation_flags);
	in_out_relation->relation_flags = flags;

	if (has_some_pose_component(flags)) {
		// First, just do the pose part (including rotating
		// derivatives, if applicable).
		transform_accumulate_pose(
		    additional_relation->pose, *in_out_relation, LEGACY,
		    has_position(flags), has_orientation(flags));
	}

	// Then, accumulate the derivatives, if required.
	if (has_lin_vel(flags)) {
		map_vec3(in_out_relation->linear_velocity) +=
		    map_vec3(additional_relation->linear_velocity);
	}

	if (has_ang_vel(flags)) {
		map_vec3(in_out_relation->angular_velocity) +=
		    map_vec3(additional_relation->angular_velocity);
	}

	if (has_lin_acc(flags)) {
		map_vec3(in_out_relation->linear_acceleration) +=
		    map_vec3(additional_relation->linear_acceleration);
	}

	if (has_ang_acc(flags)) {
		map_vec3(in_out_relation->angular_acceleration) +=
		    map_vec3(additional_relation->angular_acceleration);
	}
}

extern "C" void
math_relation_openxr_locate(const struct xrt_pose *space_pose,
                            const struct xrt_space_relation *relative_relation,
                            const struct xrt_pose *base_space_pose,
                            struct xrt_space_relation *result)
{
	assert(space_pose != NULL);
	assert(relative_relation != NULL);
	assert(base_space_pose != NULL);
	assert(result != NULL);

	// Compilers are slightly better optimizing
	// if we copy the arguments in one go.
	const auto bsp = *base_space_pose;
	const auto spc = *space_pose;
	struct xrt_space_relation accumulating_relation = BLANK_RELATION;

	// Apply the invert of the base space to identity.
	math_pose_invert(&bsp, &accumulating_relation.pose);

	// Apply the pure relation between spaces.
	math_relation_accumulate_relation(relative_relation,
	                                  &accumulating_relation);

	// Apply the space pose.
	accumulate_transform(&spc, &accumulating_relation);

	*result = accumulating_relation;
}
