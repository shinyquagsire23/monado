// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C matrix_2x2 math library.
 * @author Moses Turner <moses@collabora.com>
 *
 * @see xrt_matrix_2x2
 * @ingroup aux_math
 */

#pragma once

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void
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

static inline void
math_matrix_2x2_transform_vec2(const struct xrt_matrix_2x2 *left,
                               const struct xrt_vec2 *right,
                               struct xrt_vec2 *result_out)
{
	const struct xrt_matrix_2x2 l = *left;
	const struct xrt_vec2 r = *right;
	struct xrt_vec2 result = {l.v[0] * r.x + l.v[1] * r.y, l.v[2] * r.x + l.v[3] * r.y};
	*result_out = result;
}

#ifdef __cplusplus
}
#endif
