// Copyright 2019-2021, Collabora, Ltd.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C vec3 math library.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Nova King <technobaboo@gmail.com>
 * @author Moses Turner <moses@collabora.com>>
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

static inline struct xrt_vec3
m_vec3_project(struct xrt_vec3 project_this, struct xrt_vec3 onto_this)
{

	float amnt = (m_vec3_dot(project_this, onto_this) / m_vec3_len_sqrd(onto_this));

	return m_vec3_mul_scalar(onto_this, amnt);
}

static inline struct xrt_vec3
m_vec3_orthonormalize(struct xrt_vec3 leave_this_alone, struct xrt_vec3 change_this_one)
{
	return m_vec3_normalize(m_vec3_sub(change_this_one, m_vec3_project(change_this_one, leave_this_alone)));
}

static inline struct xrt_vec3
m_vec3_lerp(struct xrt_vec3 from, struct xrt_vec3 to, float amount)
{
	// Recommend amount being in [0,1]
	return m_vec3_add(m_vec3_mul_scalar(from, 1.0f - amount), m_vec3_mul_scalar(to, amount));
}

static inline bool
m_vec3_equal_exact(struct xrt_vec3 l, struct xrt_vec3 r)
{
	return l.x == r.x && l.y == r.y && l.z == r.z;
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

static inline struct xrt_vec3
operator*(const struct xrt_vec3 &a, const float &b)
{
	return m_vec3_mul_scalar(a, b);
}

static inline struct xrt_vec3
operator*(const struct xrt_vec3 &a, const struct xrt_vec3 &b)
{
	return m_vec3_mul(a, b);
}

static inline struct xrt_vec3
operator/(const struct xrt_vec3 &a, const struct xrt_vec3 &b)
{
	return m_vec3_div(a, b);
}

static inline struct xrt_vec3
operator/(const struct xrt_vec3 &a, const float &b)
{
	return m_vec3_div_scalar(a, b);
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
operator*=(struct xrt_vec3 &a, const float &b)
{
	a = a * b;
}

static inline void
operator/=(struct xrt_vec3 &a, const struct xrt_vec3 &b)
{
	a = a / b;
}


#endif
