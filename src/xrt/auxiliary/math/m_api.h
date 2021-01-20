// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C interface to math library.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @see xrt_vec3
 * @see xrt_quat
 * @see xrt_pose
 * @see xrt_space_relation
 * @ingroup aux_math
 */

#pragma once

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup aux_math Math
 * @ingroup aux
 *
 * @brief C interface to some transform-related math functions.
 */

/*!
 * @dir auxiliary/math
 * @ingroup aux
 *
 * @brief C interface to some transform-related math functions.
 */

/*
 *
 * Defines.
 *
 */

/*!
 * Standard gravity acceleration constant.
 *
 * @ingroup aux_math
 */
#define MATH_GRAVITY_M_S2 (9.8066)


/*
 *
 * Hash functions.
 *
 */

/*!
 * Generate a hash value from the given string, trailing zero not included.
 *
 * Hashing function used is not specified so no guarantee of staying the same
 * between different versions of the software, or even when the same version
 * is compiled on different platforms/libc++ as it might use std::hash.
 *
 * @ingroup aux_math
 */
size_t
math_hash_string(const char *str_c, size_t length);


/*
 *
 * Vector functions
 *
 */

/*!
 * Check if this vec3 is valid for math operations.
 *
 * @relates xrt_vec3
 * @ingroup aux_math
 */
bool
math_vec3_validate(const struct xrt_vec3 *vec3);

/*!
 * Accumulate a vector by adding in-place.
 *
 * Logically, *inAndOut += *additional
 * OK if the two arguments are the same addresses.
 *
 * @relates xrt_vec3
 * @ingroup aux_math
 */
void
math_vec3_accum(const struct xrt_vec3 *additional, struct xrt_vec3 *inAndOut);

/*!
 * Subtract from a vector in-place.
 *
 * Logically, *inAndOut -= *subtrahend
 * OK if the two arguments are the same addresses.
 *
 * @relates xrt_vec3
 * @ingroup aux_math
 */
void
math_vec3_subtract(const struct xrt_vec3 *subtrahend, struct xrt_vec3 *inAndOut);

/*!
 * Multiply a vector in-place.
 *
 * Logically, *inAndOut *= scalar
 *
 * @relates xrt_vec3
 * @ingroup aux_math
 */
void
math_vec3_scalar_mul(float scalar, struct xrt_vec3 *inAndOut);

/*!
 * Cross product of a vector.
 *
 * @relates xrt_vec3
 * @ingroup aux_math
 */
void
math_vec3_cross(const struct xrt_vec3 *l, const struct xrt_vec3 *r, struct xrt_vec3 *result);


/*!
 * Normalize a vec3 in place.
 *
 * @relates xrt_vec3
 * @ingroup aux_math
 */
void
math_vec3_normalize(struct xrt_vec3 *in);

/*
 *
 * Quat functions.
 *
 */

/*!
 * Create a rotation from a angle in radians and a vector.
 *
 * @relates xrt_quat
 * @relates xrt_vec3
 * @ingroup aux_math
 */
void
math_quat_from_angle_vector(float angle_rads, const struct xrt_vec3 *vector, struct xrt_quat *result);

/*!
 * Create a rotation from a 3x3 rotation matrix.
 *
 * @relates xrt_quat
 * @relates xrt_matrix_3x3
 * @ingroup aux_math
 */
void
math_quat_from_matrix_3x3(const struct xrt_matrix_3x3 *mat, struct xrt_quat *result);

/*!
 * Create a rotation from two vectors plus x and z, by creating a rotation
 * matrix by crossing z and x to get the y axis.
 *
 * @relates xrt_quat
 * @relates xrt_vec3
 * @ingroup aux_math
 */
void
math_quat_from_plus_x_z(const struct xrt_vec3 *plus_x, const struct xrt_vec3 *plus_z, struct xrt_quat *result);

/*!
 * Check if this quat can be used in transformation operations.
 *
 * @relates xrt_quat
 * @ingroup aux_math
 */
bool
math_quat_validate(const struct xrt_quat *quat);

/*!
 * Check if this quat is within 1% of unit length.
 *
 * @relates xrt_quat
 * @ingroup aux_math
 */
bool
math_quat_validate_within_1_percent(const struct xrt_quat *quat);

/*!
 * Invert a quaternion.
 *
 * @relates xrt_quat
 * @ingroup aux_math
 */
void
math_quat_invert(const struct xrt_quat *quat, struct xrt_quat *out_quat);

/*!
 * Normalize a quaternion.
 *
 * @relates xrt_quat
 * @ingroup aux_math
 */
void
math_quat_normalize(struct xrt_quat *inout);

/*!
 * Normalizes a quaternion if it has accumulated float precision errors.
 * Returns true if the quaternion was already normalized or was normalized after
 * being found within a small float precision tolerance.
 * Returns false if the quaternion was not at all normalized.
 *
 * @relates xrt_quat
 * @ingroup aux_math
 */
bool
math_quat_ensure_normalized(struct xrt_quat *inout);

/*!
 * Rotate a vector.
 *
 * @relates xrt_quat
 * @relatesalso xrt_vec3
 * @ingroup aux_math
 */
void
math_quat_rotate_vec3(const struct xrt_quat *left, const struct xrt_vec3 *right, struct xrt_vec3 *result);

/*!
 * Rotate a quaternion (compose rotations).
 *
 * @relates xrt_quat
 * @ingroup aux_math
 */
void
math_quat_rotate(const struct xrt_quat *left, const struct xrt_quat *right, struct xrt_quat *result);


/*!
 * Integrate an angular velocity vector (exponential map) and apply to a
 * quaternion.
 *
 * ang_vel and dt should share the same units of time, and the ang_vel
 * vector should be in radians per unit of time.
 *
 * @relates xrt_quat
 * @relatesalso xrt_vec3
 * @ingroup aux_math
 */
void
math_quat_integrate_velocity(const struct xrt_quat *quat,
                             const struct xrt_vec3 *ang_vel,
                             float dt,
                             struct xrt_quat *result);

/*!
 * Compute an angular velocity vector (exponential map format) by taking the
 * finite difference of two quaternions.
 *
 * quat1 is the orientation dt time after the orientation was quat0
 *
 * out_ang_vel and dt share the same units of time, and out_ang_vel is be in
 * radians per unit of time.
 *
 * @relates xrt_quat
 * @relatesalso xrt_vec3
 * @ingroup aux_math
 */
void
math_quat_finite_difference(const struct xrt_quat *quat0,
                            const struct xrt_quat *quat1,
                            float dt,
                            struct xrt_vec3 *out_ang_vel);

/*!
 * Used to rotate a derivative like a angular velocity.
 *
 * @relates xrt_quat
 * @relatesalso xrt_vec3
 * @ingroup aux_math
 */
void
math_quat_rotate_derivative(const struct xrt_quat *rot, const struct xrt_vec3 *deriv, struct xrt_vec3 *result);


/*
 *
 * Matrix functions
 *
 */

/*!
 * Multiply Matrix2x2.
 *
 * @relates xrt_matrix_2x2
 * @ingroup aux_math
 */
void
math_matrix_2x2_multiply(const struct xrt_matrix_2x2 *left,
                         const struct xrt_matrix_2x2 *right,
                         struct xrt_matrix_2x2 *result);

void
math_matrix_3x3_transform_vec3(const struct xrt_matrix_3x3 *left,
                               const struct xrt_vec3 *right,
                               struct xrt_vec3 *result);

/*!
 * Initialize Matrix4x4 with identity.
 *
 * @relates xrt_matrix_4x4
 * @ingroup aux_math
 */
void
math_matrix_4x4_identity(struct xrt_matrix_4x4 *result);

/*!
 * Multiply Matrix4x4.
 *
 * @relates xrt_matrix_4x4
 * @ingroup aux_math
 */
void
math_matrix_4x4_multiply(const struct xrt_matrix_4x4 *left,
                         const struct xrt_matrix_4x4 *right,
                         struct xrt_matrix_4x4 *result);

/*!
 * Compute view matrix from xrt_pose.
 *
 * @relates xrt_matrix_4x4
 * @ingroup aux_math
 */
void
math_matrix_4x4_view_from_pose(const struct xrt_pose *pose, struct xrt_matrix_4x4 *result);

/*!
 * Compute quad layer model matrix from xrt_pose and xrt_vec2 size.
 *
 * @relates xrt_matrix_4x4
 * @ingroup aux_math
 */
void
math_matrix_4x4_model(const struct xrt_pose *pose, const struct xrt_vec3 *size, struct xrt_matrix_4x4 *result);

/*!
 * Compute inverse view projection matrix,
 * using only the starting 3x3 block of the view.
 *
 * @relates xrt_matrix_4x4
 * @ingroup aux_math
 */
void
math_matrix_4x4_inverse_view_projection(const struct xrt_matrix_4x4 *view,
                                        const struct xrt_matrix_4x4 *projection,
                                        struct xrt_matrix_4x4 *result);

/*
 *
 * Pose functions.
 *
 */

/*!
 * Check if this pose can be used in transformation operations.
 *
 * @relates xrt_pose
 * @ingroup aux_math
 */
bool
math_pose_validate(const struct xrt_pose *pose);

/*!
 * Invert pose.
 *
 * OK if input and output are the same addresses.
 *
 * @relates xrt_pose
 * @ingroup aux_math
 */
void
math_pose_invert(const struct xrt_pose *pose, struct xrt_pose *outPose);

/*!
 * Apply a rigid-body transformation to a pose.
 *
 * OK if input and output are the same addresses.
 *
 * @relates xrt_pose
 * @ingroup aux_math
 */
void
math_pose_transform(const struct xrt_pose *transform, const struct xrt_pose *pose, struct xrt_pose *outPose);

/*!
 * Apply a rigid-body transformation to a point.
 *
 * The input point and output may be the same pointer.
 *
 * @relates xrt_pose
 * @relates xrt_vec3
 * @ingroup aux_math
 */
void
math_pose_transform_point(const struct xrt_pose *transform, const struct xrt_vec3 *point, struct xrt_vec3 *out_point);


/*
 *
 * Optics functions.
 *
 */

/*!
 * Perform the computations from
 * "Computing Half-Fields-Of-View from Simpler Display Models",
 * to get half-FOVs from things we can retrieve from other APIs.
 * The origin is in the lower-left corner of the display, so w_1 is the width to
 * the left of CoP, and h_1 is the height below CoP.
 *
 * If vertfov_total is set to 0, it will be computed from h_total.
 *
 * Distances are in arbitrary but consistent units. Angles are in radians.
 *
 *
 * In the diagram below, treating it like a FOV for horizontal,
 * the top angle is horizfov_total, the length of the bottom
 * is w_total, and the distance between the vertical line and the left corner is
 * w_1. Vertical is similar - h_1 is above the center line.
 * The triangle need not be symmetrical, despite how the diagram looks.
 *
 * ```
 *               horizfov_total
 *                       *
 * angle_left (neg) -> / |  \ <- angle_right
 *                    /  |   \
 *                   /   |    \
 *                  /    |     \
 *                 -------------
 *                 [ w_1 ]
 *                 [ --- w  --- ]
 *
 * -------     --- |\
 *                 |   \
 *             h_1 |      \ angle_up
 * h_total     ___ |-------* vertfov_total
 *                 |      / angle_down (neg)
 *                 |    /
 *                 |  /
 * -------         |/
 * ```
 *
 * @return true if successful.
 * @ingroup aux_math
 */
bool
math_compute_fovs(double w_total,
                  double w_1,
                  double horizfov_total,
                  double h_total,
                  double h_1,
                  double vertfov_total,
                  struct xrt_fov *fov);

#ifdef __cplusplus
}
#endif
