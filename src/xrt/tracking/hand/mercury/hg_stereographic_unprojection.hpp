// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Stereographic unprojection
 * @author Moshi Turner <moses@collabora.com>
 * @ingroup tracking
 */

#pragma once
#include "math/m_eigen_interop.hpp"

static inline Eigen::Vector3f
stereographic_unprojection(float sg_x, float sg_y)
{
	float X = sg_x;
	float Y = sg_y;

	float denom = (1 + X * X + Y * Y);

	float x = (2 * X) / denom;
	float y = (2 * Y) / denom;
	float z = (-1 + X * X + Y * Y) / denom;

	// forward is -z
	return {x, y, z};
	// return {x / -z, y / -z};
}
