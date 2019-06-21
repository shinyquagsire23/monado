// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Base implementations for math library.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <assert.h>

#include "math/m_api.h"
#include "math/m_eigen_interop.h"

/*
 *
 * Copy helpers.
 *
 */

static inline Eigen::Quaternionf
copy(const struct xrt_quat& q)
{
	// Eigen constructor order is different from XRT, OpenHMD and OpenXR!
	//  Eigen: `float w, x, y, z`.
	// OpenXR: `float x, y, z, w`.
	return Eigen::Quaternionf(q.w, q.x, q.y, q.z);
}

static inline Eigen::Quaternionf
copy(const struct xrt_quat* q)
{
	return copy(*q);
}

static inline Eigen::Vector3f
copy(const struct xrt_vec3& v)
{
	return Eigen::Vector3f(v.x, v.y, v.z);
}

static inline Eigen::Vector3f
copy(const struct xrt_vec3* v)
{
	return copy(*v);
}


/*
 *
 * Exported vector functions.
 *
 */
void
math_vec3_accum(const struct xrt_vec3* additional, struct xrt_vec3* inAndOut)
{
	assert(additional != NULL);
	assert(inAndOut != NULL);

	map_vec3(*inAndOut) += map_vec3(*additional);
}

/*
 *
 * Exported quaternion functions.
 *
 */

void
math_quat_normalize(struct xrt_quat* inout)
{
	assert(inout != NULL);
	map_quat(*inout).normalize();
}

void
math_quat_rotate(const struct xrt_quat* left,
                 const struct xrt_quat* right,
                 struct xrt_quat* result)
{
	assert(left != NULL);
	assert(right != NULL);
	assert(result != NULL);

	auto l = copy(left);
	auto r = copy(right);

	auto q = l * r;

	map_quat(*result) = q;
}

void
math_quat_rotate_vec3(const struct xrt_quat* left,
                      const struct xrt_vec3* right,
                      struct xrt_vec3* result)
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
 * Exported pose functions.
 *
 */

bool
math_pose_validate(const struct xrt_pose* pose)
{
	assert(pose != NULL);

	const float FLOAT_EPSILON = Eigen::NumTraits<float>::epsilon();
	auto norm = orientation(*pose).squaredNorm();
	if (norm > 1.0f + FLOAT_EPSILON || norm < 1.0f - FLOAT_EPSILON) {
		return false;
	}

	// Technically not yet a required check, but easier to stop problems
	// now than once denormalized numbers pollute the rest of our state.
	// see https://gitlab.khronos.org/openxr/openxr/issues/922
	if (!orientation(*pose).coeffs().allFinite()) {
		return false;
	}
	if (!position(*pose).allFinite()) {
		return false;
	}
	return true;
}

void
math_pose_invert(const struct xrt_pose* pose, struct xrt_pose* outPose)
{
	assert(pose != NULL);
	assert(outPose != NULL);

	// store results to temporary locals so we can do this "in-place"
	// (pose == outPose) if desired.
	Eigen::Vector3f newPosition = -position(*pose);
	// Conjugate legal here since pose must be normalized/unit length.
	Eigen::Quaternionf newOrientation = orientation(*pose).conjugate();

	position(*outPose) = newPosition;
	orientation(*outPose) = newOrientation;
}

/*!
 * Return the result of transforming a point by a pose/transform.
 */
static inline Eigen::Vector3f
transform_point(const xrt_pose& transform, const xrt_vec3& point)
{
	return orientation(transform) * map_vec3(point) + position(transform);
}

/*!
 * Return the result of transforming a pose by a pose/transform.
 */
static inline xrt_pose
transform_pose(const xrt_pose& transform, const xrt_pose& pose)
{
	xrt_pose ret;
	position(ret) = transform_point(transform, pose.position);
	orientation(ret) = orientation(transform) * orientation(pose);
	return ret;
}

void
math_pose_transform(const struct xrt_pose* transform,
                    const struct xrt_pose* pose,
                    struct xrt_pose* outPose)
{
	assert(pose != NULL);
	assert(transform != NULL);
	assert(outPose != NULL);

	xrt_pose newPose = transform_pose(*transform, *pose);
	memcpy(outPose, &newPose, sizeof(xrt_pose));
}

void
math_pose_openxr_locate(const struct xrt_pose* space_pose,
                        const struct xrt_pose* relative_pose,
                        const struct xrt_pose* base_space_pose,
                        struct xrt_pose* result)
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
rotate_deriv(Eigen::Matrix3f const& rotation,
             const xrt_vec3& derivativeVector,
             Eigen::Matrix3f const& rotationInverse)
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

/*!
 * Apply a transform to a space relation.
 */
static inline void
transform_accumulate_pose(const xrt_pose& transform,
                          xrt_space_relation& relation,
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

		math_pose_transform(&in_out_relation->pose, &transform,
		                    &in_out_relation->pose);
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

void
math_relation_reset(struct xrt_space_relation* out)
{
	*out = BLANK_RELATION;
}

void
math_relation_accumulate_transform(const struct xrt_pose* transform,
                                   struct xrt_space_relation* in_out_relation)
{
	assert(transform != nullptr);
	assert(in_out_relation != nullptr);

	// No modifying the validity flags here.
	transform_accumulate_pose(*transform, *in_out_relation);
}


void
math_relation_accumulate_relation(
    const struct xrt_space_relation* additional_relation,
    struct xrt_space_relation* in_out_relation)
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
		transform_accumulate_pose(additional_relation->pose,
		                          *in_out_relation, has_position(flags),
		                          has_orientation(flags));
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

void
math_relation_openxr_locate(const struct xrt_pose* space_pose,
                            const struct xrt_space_relation* relative_relation,
                            const struct xrt_pose* base_space_pose,
                            struct xrt_space_relation* result)
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
	math_relation_accumulate_transform(&spc, &accumulating_relation);

	*result = accumulating_relation;
}
