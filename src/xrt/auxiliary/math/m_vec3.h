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

#include "m_mathinclude.h"
#include <float.h>


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
m_vec3_dot(struct xrt_vec3 l, struct xrt_vec3 r)
{
	return l.x * r.x + l.y * r.y + l.z * r.z;
}

static inline float
m_vec3_len_sqrd(struct xrt_vec3 l)
{
	return m_vec3_dot(l, l);
}

static inline float
m_vec3_len(struct xrt_vec3 l)
{
	return sqrtf(m_vec3_len_sqrd(l));
}

static inline struct xrt_vec3
m_vec3_normalize(struct xrt_vec3 l)
{
	float len = m_vec3_len(l);
	if (len <= FLT_EPSILON) {
		return l;
	}

	struct xrt_vec3 ret = {
	    l.x / len,
	    l.y / len,
	    l.z / len,
	};
	return ret;
}

static inline float
m_vec3_angle(struct xrt_vec3 l, struct xrt_vec3 r)
{
	float dot = m_vec3_dot(l, r);
	float lengths = m_vec3_len_sqrd(l) * m_vec3_len_sqrd(r);

	if (lengths == 0) {
		return 0;
	}

	return acosf(dot / lengths);
}


#ifdef __cplusplus
}

static inline struct xrt_vec3
operator+(const struct xrt_vec3 &a, const struct xrt_vec3 &b)
{
	return m_vec3_add(a, b);
}

static inline struct xrt_vec3
operator-(const struct xrt_vec3 &a, const struct xrt_vec3 &b)
{
	return m_vec3_sub(a, b);
}

static inline struct xrt_vec3 // Until clang-format-11 is on the CI.
operator*(const struct xrt_vec3 &a, const struct xrt_vec3 &b)
{
	return m_vec3_mul(a, b);
}

static inline struct xrt_vec3
operator/(const struct xrt_vec3 &a, const struct xrt_vec3 &b)
{
	return m_vec3_div(a, b);
}

static inline void
operator+=(struct xrt_vec3 &a, const struct xrt_vec3 &b)
{
	a = a + b;
}

static inline void
operator-=(struct xrt_vec3 &a, const struct xrt_vec3 &b)
{
	a = a - b;
}

static inline void
operator*=(struct xrt_vec3 &a, const struct xrt_vec3 &b)
{
	a = a * b;
}

static inline void
operator/=(struct xrt_vec3 &a, const struct xrt_vec3 &b)
{
	a = a / b;
}


#endif
