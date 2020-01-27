// Copyright 2019-2020, Collabora, Ltd.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C vec3 math library.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Nova King <technobaboo@gmail.com>
 *
 * @see xrt_vec3
 * @ingroup aux_math
 */

#pragma once

#include "xrt/xrt_defines.h"

#include <math.h>


#ifdef __cplusplus
extern "C" {
#endif


static inline struct xrt_vec3
m_vec3_mul(struct xrt_vec3 l, struct xrt_vec3 r)
{
	struct xrt_vec3 ret = {l.x * r.x, l.y * r.y, l.z * r.z};
	return ret;
}

static inline struct xrt_vec3
m_vec3_mul_scalar(struct xrt_vec3 l, float r)
{
	struct xrt_vec3 ret = {l.x * r, l.y * r, l.z * r};
	return ret;
}

static inline struct xrt_vec3
m_vec3_add(struct xrt_vec3 l, struct xrt_vec3 r)
{
	struct xrt_vec3 ret = {l.x + r.x, l.y + r.y, l.z + r.z};
	return ret;
}

static inline struct xrt_vec3
m_vec3_sub(struct xrt_vec3 l, struct xrt_vec3 r)
{
	struct xrt_vec3 ret = {l.x - r.x, l.y - r.y, l.z - r.z};
	return ret;
}

static inline struct xrt_vec3
m_vec3_div(struct xrt_vec3 l, struct xrt_vec3 r)
{
	struct xrt_vec3 ret = {l.x / r.x, l.y / r.y, l.z / r.z};
	return ret;
}

static inline struct xrt_vec3
m_vec3_div_scalar(struct xrt_vec3 l, float r)
{
	struct xrt_vec3 ret = {l.x / r, l.y / r, l.z / r};
	return ret;
}

static inline float
m_vec3_len(struct xrt_vec3 l)
{
	return sqrtf(l.x * l.x + l.y * l.y + l.z * l.z);
}


#ifdef __cplusplus
}
#endif
