// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Base implementations for math library.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Moses Turner <mosesturner@protonmail.com>
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @ingroup aux_math
 */

#include "math/m_api.h"
#include "math/m_eigen_interop.hpp"
#include "math/m_vec3.h"

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <assert.h>

using namespace xrt::auxiliary::math;

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

XRT_MAYBE_UNUSED static inline Eigen::Quaterniond
copyd(const struct xrt_quat &q)
{
	// Eigen constructor order is different from XRT, OpenHMD and OpenXR!
	//  Eigen: `float w, x, y, z`.
	// OpenXR: `float x, y, z, w`.
	return Eigen::Quaterniond(q.w, q.x, q.y, q.z);
}

XRT_MAYBE_UNUSED static inline Eigen::Quaterniond
copyd(const struct xrt_quat *q)
{
	return copyd(*q);
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


static inline Eigen::Vector3d
copy(const struct xrt_vec3_f64 &v)
{
	return Eigen::Vector3d(v.x, v.y, v.z);
}

static inline Eigen::Vector3d
copy(const struct xrt_vec3_f64 *v)
{
	return copy(*v);
}

XRT_MAYBE_UNUSED static inline Eigen::Vector3d
copyd(const struct xrt_vec3 &v)
{
	return Eigen::Vector3d(v.x, v.y, v.z);
}

XRT_MAYBE_UNUSED static inline Eigen::Vector3d
copyd(const struct xrt_vec3 *v)
{
	return copyd(*v);
}

static inline Eigen::Matrix3f
copy(const struct xrt_matrix_3x3 *m)
{
	Eigen::Matrix3f res;
	// clang-format off
	res << m->v[0], m->v[3], m->v[6],
	       m->v[1], m->v[4], m->v[7],
	       m->v[2], m->v[5], m->v[8];
	// clang-format on
	return res;
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
math_vec3_subtract(const struct xrt_vec3 *subtrahend, struct xrt_vec3 *inAndOut)
{
	assert(subtrahend != NULL);
	assert(inAndOut != NULL);

	map_vec3(*inAndOut) -= map_vec3(*subtrahend);
}

extern "C" void
math_vec3_scalar_mul(float scalar, struct xrt_vec3 *inAndOut)
{
	assert(inAndOut != NULL);

	map_vec3(*inAndOut) *= scalar;
}

extern "C" void
math_vec3_cross(const struct xrt_vec3 *l, const struct xrt_vec3 *r, struct xrt_vec3 *result)
{
	map_vec3(*result) = map_vec3(*l).cross(map_vec3(*r));
}

extern "C" void
math_vec3_normalize(struct xrt_vec3 *in)
{
	map_vec3(*in) = map_vec3(*in).normalized();
}

extern "C" void
math_vec3_f64_normalize(struct xrt_vec3_f64 *in)
{
	map_vec3_f64(*in) = map_vec3_f64(*in).normalized();
}

/*
 *
 * Exported quaternion functions.
 *
 */

extern "C" void
math_quat_from_angle_vector(float angle_rads, const struct xrt_vec3 *vector, struct xrt_quat *result)
{
	map_quat(*result) = Eigen::AngleAxisf(angle_rads, copy(vector));
}

extern "C" void
math_quat_from_matrix_3x3(const struct xrt_matrix_3x3 *mat, struct xrt_quat *result)
{
	Eigen::Matrix3f m;
	m << mat->v[0], mat->v[1], mat->v[2], mat->v[3], mat->v[4], mat->v[5], mat->v[6], mat->v[7], mat->v[8];

	Eigen::Quaternionf q(m);
	map_quat(*result) = q;
}

extern "C" void
math_quat_from_plus_x_z(const struct xrt_vec3 *plus_x, const struct xrt_vec3 *plus_z, struct xrt_quat *result)
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

static bool
quat_validate(const float precision, const struct xrt_quat *quat)
{
	assert(quat != NULL);
	auto rot = copy(*quat);


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
	if (norm > 1.0f + precision || norm < 1.0f - precision) {
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

extern "C" bool
math_quat_validate(const struct xrt_quat *quat)
{
	const float FLOAT_EPSILON = Eigen::NumTraits<float>::epsilon();
	return quat_validate(FLOAT_EPSILON, quat);
}

extern "C" bool
math_quat_validate_within_1_percent(const struct xrt_quat *quat)
{
	return quat_validate(0.01f, quat);
}

extern "C" void
math_quat_invert(const struct xrt_quat *quat, struct xrt_quat *out_quat)
{
	map_quat(*out_quat) = map_quat(*quat).conjugate();
}

extern "C" float
math_quat_len(const struct xrt_quat *quat)
{
	return map_quat(*quat).norm();
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
math_quat_rotate(const struct xrt_quat *left, const struct xrt_quat *right, struct xrt_quat *result)
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
math_quat_unrotate(const struct xrt_quat *left, const struct xrt_quat *right, struct xrt_quat *result)
{
	assert(left != NULL);
	assert(right != NULL);
	assert(result != NULL);

	auto l = copy(left);
	auto r = copy(right);

	auto q = l.inverse() * r;

	map_quat(*result) = q;
}

extern "C" void
math_quat_rotate_vec3(const struct xrt_quat *left, const struct xrt_vec3 *right, struct xrt_vec3 *result)
{
	assert(left != NULL);
	assert(right != NULL);
	assert(result != NULL);

	auto l = copy(left);
	auto r = copy(right);

	auto v = l * r;

	map_vec3(*result) = v;
}

extern "C" void
math_quat_rotate_derivative(const struct xrt_quat *quat, const struct xrt_vec3 *deriv, struct xrt_vec3 *result)
{
	assert(quat != NULL);
	assert(deriv != NULL);
	assert(result != NULL);

	auto l = copy(quat);
	auto m = Eigen::Quaternionf(0.0f, deriv->x, deriv->y, deriv->z);
	auto r = l.conjugate();

	auto v = l * m * r;

	struct xrt_vec3 ret = {v.x(), v.y(), v.z()};
	*result = ret;
}

extern "C" void
math_quat_slerp(const struct xrt_quat *left, const struct xrt_quat *right, float t, struct xrt_quat *result)
{
	assert(left != NULL);
	assert(right != NULL);
	assert(result != NULL);

	auto l = copy(left);
	auto r = copy(right);

	map_quat(*result) = l.slerp(t, r);
}

extern "C" void
math_quat_from_swing(const struct xrt_vec2 *swing, struct xrt_quat *result)
{
	assert(swing != NULL);
	assert(result != NULL);
	const float *a0 = &swing->x;
	const float *a1 = &swing->y;
	const float theta_squared = *a0 * *a0 + *a1 * *a1;

	if (theta_squared > 0.f) {
		const float theta = sqrt(theta_squared);
		const float half_theta = theta * 0.5f;
		const float k = sin(half_theta) / theta;
		result->w = cos(half_theta);
		result->x = *a0 * k;
		result->y = *a1 * k;
		result->z = 0.f;
	} else {
		// lim(x->0) (sin(x/2)/x) = 0.5, but sin(0)/0 is undefined, so we need to catch this with a conditional.
		const float k = 0.5f;
		result->w = 1.0f;
		result->x = *a0 * k;
		result->y = *a1 * k;
		result->z = 0.f;
	}
}

extern "C" void
math_quat_from_swing_twist(const struct xrt_vec2 *swing, const float twist, struct xrt_quat *result)
{
	assert(swing != NULL);
	assert(result != NULL);

	struct xrt_quat swing_quat;
	struct xrt_quat twist_quat;

	struct xrt_vec3 aax_twist;

	aax_twist.x = 0.f;
	aax_twist.y = 0.f;
	aax_twist.z = twist;

	math_quat_from_swing(swing, &swing_quat);

	math_quat_exp(&aax_twist, &twist_quat);

	math_quat_rotate(&swing_quat, &twist_quat, result);
}

/*
 *
 * Exported matrix functions.
 *
 */

extern "C" void
math_matrix_2x2_multiply(const struct xrt_matrix_2x2 *left,
                         const struct xrt_matrix_2x2 *right,
                         struct xrt_matrix_2x2 *result_out)
{
	const struct xrt_matrix_2x2 l = *left;
	const struct xrt_matrix_2x2 r = *right;

	// Initialisers: struct, union, v[4]
	struct xrt_matrix_2x2 result = {{{
	    l.v[0] * r.v[0] + l.v[1] * r.v[2],
	    l.v[0] * r.v[1] + l.v[1] * r.v[3],
	    l.v[2] * r.v[0] + l.v[3] * r.v[2],
	    l.v[2] * r.v[1] + l.v[3] * r.v[3],
	}}};

	*result_out = result;
}

extern "C" void
math_matrix_2x2_transform_vec2(const struct xrt_matrix_2x2 *left,
                               const struct xrt_vec2 *right,
                               struct xrt_vec2 *result_out)
{
	const struct xrt_matrix_2x2 l = *left;
	const struct xrt_vec2 r = *right;
	struct xrt_vec2 result = {l.v[0] * r.x + l.v[1] * r.y, l.v[2] * r.x + l.v[3] * r.y};
	*result_out = result;
}

extern "C" void
math_matrix_3x3_identity(struct xrt_matrix_3x3 *mat)
{
	map_matrix_3x3(*mat) = Eigen::Matrix3f::Identity();
}

extern "C" void
math_matrix_3x3_from_quat(const struct xrt_quat *q, struct xrt_matrix_3x3 *result_out)
{
	struct xrt_matrix_3x3 result = {{
	    1 - 2 * q->y * q->y - 2 * q->z * q->z,
	    2 * q->x * q->y - 2 * q->w * q->z,
	    2 * q->x * q->z + 2 * q->w * q->y,

	    2 * q->x * q->y + 2 * q->w * q->z,
	    1 - 2 * q->x * q->x - 2 * q->z * q->z,
	    2 * q->y * q->z - 2 * q->w * q->x,

	    2 * q->x * q->z - 2 * q->w * q->y,
	    2 * q->y * q->z + 2 * q->w * q->x,
	    1 - 2 * q->x * q->x - 2 * q->y * q->y,
	}};

	*result_out = result;
}

extern "C" void
math_matrix_3x3_f64_identity(struct xrt_matrix_3x3_f64 *mat)
{
	map_matrix_3x3_f64(*mat) = Eigen::Matrix3d::Identity();
}

extern "C" void
math_matrix_3x3_f64_transform_vec3_f64(const struct xrt_matrix_3x3_f64 *left,
                                       const struct xrt_vec3_f64 *right,
                                       struct xrt_vec3_f64 *result_out)
{
	Eigen::Matrix3d m;
	m << left->v[0], left->v[1], left->v[2], // 1
	    left->v[3], left->v[4], left->v[5],  // 2
	    left->v[6], left->v[7], left->v[8];  // 3

	map_vec3_f64(*result_out) = m * copy(right);
}

extern "C" void
math_matrix_3x3_f64_from_plus_x_z(const struct xrt_vec3_f64 *plus_x,
                                  const struct xrt_vec3_f64 *plus_z,
                                  struct xrt_matrix_3x3_f64 *result)
{
	xrt_vec3_f64 plus_y;
	math_vec3_f64_cross(plus_z, plus_x, &plus_y);

	result->v[0] = plus_x->x;
	result->v[1] = plus_y.x;
	result->v[2] = plus_z->x;
	result->v[3] = plus_x->y;
	result->v[4] = plus_y.y;
	result->v[5] = plus_z->y;
	result->v[6] = plus_x->z;
	result->v[7] = plus_y.z;
	result->v[8] = plus_z->z;
}

extern "C" void
math_matrix_3x3_rotation_from_isometry(const struct xrt_matrix_4x4 *isometry, struct xrt_matrix_3x3 *result)
{
	Eigen::Isometry3f transform{map_matrix_4x4(*isometry)};
	map_matrix_3x3(*result) = transform.linear();
}

extern "C" void
math_matrix_3x3_transform_vec3(const struct xrt_matrix_3x3 *left,
                               const struct xrt_vec3 *right,
                               struct xrt_vec3 *result_out)
{
	Eigen::Matrix3f m;
	m << left->v[0], left->v[1], left->v[2], // 1
	    left->v[3], left->v[4], left->v[5],  // 2
	    left->v[6], left->v[7], left->v[8];  // 3

	map_vec3(*result_out) = m * copy(right);
}

extern "C" void
math_matrix_4x4_transform_vec3(const struct xrt_matrix_4x4 *left,
                               const struct xrt_vec3 *right,
                               struct xrt_vec3 *result_out)
{
	Eigen::Matrix4f m = copy(left);

	Eigen::Vector4f v;
	v << right->x, right->y, right->z, 1.0;

	Eigen::Vector4f res;
	res = m * v;

	result_out->x = res.x();
	result_out->y = res.y();
	result_out->z = res.z();
}

extern "C" void
math_matrix_3x3_multiply(const struct xrt_matrix_3x3 *left,
                         const struct xrt_matrix_3x3 *right,
                         struct xrt_matrix_3x3 *result_out)
{
	const struct xrt_matrix_3x3 l = *left;
	const struct xrt_matrix_3x3 r = *right;

	struct xrt_matrix_3x3 result = {{
	    l.v[0] * r.v[0] + l.v[1] * r.v[3] + l.v[2] * r.v[6],
	    l.v[0] * r.v[1] + l.v[1] * r.v[4] + l.v[2] * r.v[7],
	    l.v[0] * r.v[2] + l.v[1] * r.v[5] + l.v[2] * r.v[8],
	    l.v[3] * r.v[0] + l.v[4] * r.v[3] + l.v[5] * r.v[6],
	    l.v[3] * r.v[1] + l.v[4] * r.v[4] + l.v[5] * r.v[7],
	    l.v[3] * r.v[2] + l.v[4] * r.v[5] + l.v[5] * r.v[8],
	    l.v[6] * r.v[0] + l.v[7] * r.v[3] + l.v[8] * r.v[6],
	    l.v[6] * r.v[1] + l.v[7] * r.v[4] + l.v[8] * r.v[7],
	    l.v[6] * r.v[2] + l.v[7] * r.v[5] + l.v[8] * r.v[8],
	}};

	*result_out = result;
}

extern "C" void
math_matrix_3x3_inverse(const struct xrt_matrix_3x3 *in, struct xrt_matrix_3x3 *result)
{
	Eigen::Matrix3f m = copy(in);
	map_matrix_3x3(*result) = m.inverse();
}

extern "C" void
math_matrix_3x3_transpose(const struct xrt_matrix_3x3 *in, struct xrt_matrix_3x3 *result)
{
	Eigen::Matrix3f m = copy(in);
	map_matrix_3x3(*result) = m.transpose();
}

extern "C" void
math_matrix_4x4_identity(struct xrt_matrix_4x4 *result)
{
	map_matrix_4x4(*result) = Eigen::Matrix4f::Identity();
}

extern "C" void
math_matrix_4x4_multiply(const struct xrt_matrix_4x4 *left,
                         const struct xrt_matrix_4x4 *right,
                         struct xrt_matrix_4x4 *result)
{
	map_matrix_4x4(*result) = copy(left) * copy(right);
}

extern "C" void
math_matrix_4x4_inverse(const struct xrt_matrix_4x4 *in, struct xrt_matrix_4x4 *result)
{
	Eigen::Matrix4f m = copy(in);
	map_matrix_4x4(*result) = m.inverse();
}

extern "C" void
math_matrix_4x4_transpose(const struct xrt_matrix_4x4 *in, struct xrt_matrix_4x4 *result)
{
	Eigen::Matrix4f m = copy(in);
	map_matrix_4x4(*result) = m.transpose();
}

extern "C" void
math_matrix_4x4_isometry_inverse(const struct xrt_matrix_4x4 *in, struct xrt_matrix_4x4 *result)
{
	Eigen::Isometry3f m{copy(in)};
	map_matrix_4x4(*result) = m.inverse().matrix();
}

extern "C" void
math_matrix_4x4_view_from_pose(const struct xrt_pose *pose, struct xrt_matrix_4x4 *result)
{
	Eigen::Vector3f position = copy(&pose->position);
	Eigen::Quaternionf orientation = copy(&pose->orientation);

	Eigen::Translation3f translation(position);
	Eigen::Isometry3f transformation = translation * orientation;

	map_matrix_4x4(*result) = transformation.inverse().matrix();
}

extern "C" void
math_matrix_4x4_isometry_from_rt(const struct xrt_matrix_3x3 *rotation,
                                 const struct xrt_vec3 *translation,
                                 struct xrt_matrix_4x4 *result)
{
	Eigen::Isometry3f transformation = Eigen::Isometry3f::Identity();
	transformation.linear() = map_matrix_3x3(*rotation);
	transformation.translation() = map_vec3(*translation);
	map_matrix_4x4(*result) = transformation.matrix();
}

extern "C" void
math_matrix_4x4_isometry_from_pose(const struct xrt_pose *pose, struct xrt_matrix_4x4 *result)
{
	Eigen::Isometry3f transform{Eigen::Translation3f{position(*pose)} * orientation(*pose)};
	map_matrix_4x4(*result) = transform.matrix();
}

extern "C" void
math_matrix_4x4_model(const struct xrt_pose *pose, const struct xrt_vec3 *size, struct xrt_matrix_4x4 *result)
{
	Eigen::Vector3f position = copy(&pose->position);
	Eigen::Quaternionf orientation = copy(&pose->orientation);

	auto scale = Eigen::Scaling(size->x, size->y, size->z);

	Eigen::Translation3f translation(position);
	Eigen::Affine3f transformation = translation * orientation * scale;

	map_matrix_4x4(*result) = transformation.matrix();
}

extern "C" void
math_matrix_4x4_inverse_view_projection(const struct xrt_matrix_4x4 *view,
                                        const struct xrt_matrix_4x4 *projection,
                                        struct xrt_matrix_4x4 *result)
{
	Eigen::Matrix4f v = copy(view);
	Eigen::Matrix4f v3 = Eigen::Matrix4f::Identity();
	v3.block<3, 3>(0, 0) = v.block<3, 3>(0, 0);
	Eigen::Matrix4f vp = copy(projection) * v3;
	map_matrix_4x4(*result) = vp.inverse();
}


/*
 *
 * Exported Matrix 4x4 functions.
 *
 */

extern "C" void
m_mat4_f64_identity(struct xrt_matrix_4x4_f64 *result)
{
	map_matrix_4x4_f64(*result) = Eigen::Matrix4d::Identity();
}

extern "C" void
m_mat4_f64_invert(const struct xrt_matrix_4x4_f64 *matrix, struct xrt_matrix_4x4_f64 *result)
{
	Eigen::Matrix4d m = map_matrix_4x4_f64(*matrix);
	map_matrix_4x4_f64(*result) = m.inverse();
}

extern "C" void
m_mat4_f64_multiply(const struct xrt_matrix_4x4_f64 *left,
                    const struct xrt_matrix_4x4_f64 *right,
                    struct xrt_matrix_4x4_f64 *result)
{
	Eigen::Matrix4d l = map_matrix_4x4_f64(*left);
	Eigen::Matrix4d r = map_matrix_4x4_f64(*right);

	map_matrix_4x4_f64(*result) = l * r;
}

extern "C" void
math_vec3_f64_cross(const struct xrt_vec3_f64 *l, const struct xrt_vec3_f64 *r, struct xrt_vec3_f64 *result)
{
	map_vec3_f64(*result) = map_vec3_f64(*l).cross(map_vec3_f64(*r));
}

extern "C" void
math_vec3_translation_from_isometry(const struct xrt_matrix_4x4 *transform, struct xrt_vec3 *result)
{
	Eigen::Isometry3f isometry{map_matrix_4x4(*transform)};
	map_vec3(*result) = isometry.translation();
}

extern "C" void
m_mat4_f64_orientation(const struct xrt_quat *quat, struct xrt_matrix_4x4_f64 *result)
{
	map_matrix_4x4_f64(*result) = Eigen::Affine3d(copyd(*quat)).matrix();
}

extern "C" void
m_mat4_f64_model(const struct xrt_pose *pose, const struct xrt_vec3 *size, struct xrt_matrix_4x4_f64 *result)
{
	Eigen::Vector3d position = copyd(pose->position);
	Eigen::Quaterniond orientation = copyd(pose->orientation);

	auto scale = Eigen::Scaling(copyd(size));

	Eigen::Translation3d translation(position);
	Eigen::Affine3d transformation = translation * orientation * scale;

	map_matrix_4x4_f64(*result) = transformation.matrix();
}

extern "C" void
m_mat4_f64_view(const struct xrt_pose *pose, struct xrt_matrix_4x4_f64 *result)
{
	Eigen::Vector3d position = copyd(pose->position);
	Eigen::Quaterniond orientation = copyd(pose->orientation);

	Eigen::Translation3d translation(position);
	Eigen::Isometry3d transformation = translation * orientation;

	map_matrix_4x4_f64(*result) = transformation.inverse().matrix();
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

	return math_vec3_validate(&pose->position) && math_quat_validate(&pose->orientation);
}

extern "C" void
math_pose_invert(const struct xrt_pose *pose, struct xrt_pose *outPose)
{
	Eigen::Isometry3f transform{Eigen::Translation3f{position(*pose)} * orientation(*pose)};
	Eigen::Isometry3f inverse = transform.inverse();
	position(*outPose) = inverse.translation();
	orientation(*outPose) = inverse.rotation();
}

extern "C" void
math_pose_from_isometry(const struct xrt_matrix_4x4 *transform, struct xrt_pose *result)
{
	Eigen::Isometry3f isometry{map_matrix_4x4(*transform)};
	position(*result) = isometry.translation();
	orientation(*result) = isometry.rotation();
}

extern "C" void
math_pose_interpolate(const struct xrt_pose *a, const struct xrt_pose *b, float t, struct xrt_pose *outPose)
{
	math_quat_slerp(&a->orientation, &b->orientation, t, &outPose->orientation);
	outPose->position = m_vec3_lerp(a->position, b->position, t);
}

extern "C" void
math_pose_identity(struct xrt_pose *pose)
{
	pose->position.x = 0.0;
	pose->position.y = 0.0;
	pose->position.z = 0.0;
	pose->orientation.x = 0.0;
	pose->orientation.y = 0.0;
	pose->orientation.z = 0.0;
	pose->orientation.w = 1.0;
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
math_pose_transform(const struct xrt_pose *transform, const struct xrt_pose *pose, struct xrt_pose *outPose)
{
	assert(pose != NULL);
	assert(transform != NULL);
	assert(outPose != NULL);

	xrt_pose newPose = transform_pose(*transform, *pose);
	memcpy(outPose, &newPose, sizeof(xrt_pose));
}

extern "C" void
math_pose_transform_point(const struct xrt_pose *transform, const struct xrt_vec3 *point, struct xrt_vec3 *out_point)
{
	assert(transform != NULL);
	assert(point != NULL);
	assert(out_point != NULL);

	map_vec3(*out_point) = transform_point(*transform, *point);
}
