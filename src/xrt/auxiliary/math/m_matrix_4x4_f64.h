// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C matrix 4x4 f64 math library.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @see xrt_matrix_4x4_f64
 * @ingroup aux_math
 */

#pragma once

#include "xrt/xrt_defines.h"

#include "m_mathinclude.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Initialize Matrix4x4 F64 with identity.
 *
 * @relates xrt_matrix_4x4_f64
 * @ingroup aux_math
 */
void
m_mat4_f64_identity(struct xrt_matrix_4x4_f64 *result);

/*!
 * Invert a Matrix4x4 F64.
 *
 * @relates xrt_matrix_4x4_f64
 * @ingroup aux_math
 */
void
m_mat4_f64_invert(const struct xrt_matrix_4x4_f64 *matrix, struct xrt_matrix_4x4_f64 *result);

/*!
 * Multiply Matrix4x4 F64.
 *
 * @relates xrt_matrix_4x4_f64
 * @ingroup aux_math
 */
void
m_mat4_f64_multiply(const struct xrt_matrix_4x4_f64 *left,
                    const struct xrt_matrix_4x4_f64 *right,
                    struct xrt_matrix_4x4_f64 *result);

/*!
 * Initialize Matrix4x4 F64 with a orientation.
 *
 * @relates xrt_matrix_4x4_f64
 * @ingroup aux_math
 */
void
m_mat4_f64_orientation(const struct xrt_quat *quat, struct xrt_matrix_4x4_f64 *result);

/*!
 * Initialize Matrix4x4 F64 with a pose and size that can be used as a model matrix.
 *
 * @relates xrt_matrix_4x4_f64
 * @ingroup aux_math
 */
void
m_mat4_f64_model(const struct xrt_pose *pose, const struct xrt_vec3 *size, struct xrt_matrix_4x4_f64 *result);

/*!
 * Initialize Matrix4x4 F64 with a pose that can be used as a view martix.
 *
 * @relates xrt_matrix_4x4_f64
 * @ingroup aux_math
 */
void
m_mat4_f64_view(const struct xrt_pose *pose, const struct xrt_vec3 *size, struct xrt_matrix_4x4_f64 *result);


#ifdef __cplusplus
}


static inline struct xrt_matrix_4x4_f64 // Until clang-format-11 is on the CI.
operator*(const struct xrt_matrix_4x4_f64 &a, const struct xrt_matrix_4x4_f64 &b)
{
	struct xrt_matrix_4x4_f64 ret = {{0}};
	m_mat4_f64_multiply(&l, &r, &ret);
	return ret;
}

static inline void
operator*=(struct xrt_matrix_4x4_f64 &a, const struct xrt_matrix_4x4_f64 &b)
{
	a = a * b;
}


#endif
