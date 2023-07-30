// Copyright 2021-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to deal with bounding boxes for camera-based hand-tracking.
 * @author Moses Turner <moses@collabora.com>
 * @author Marcus Edel <marcus.edel@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <math.h>
#include "xrt/xrt_defines.h"

namespace xrt::auxiliary::util::box_iou {
struct Box
{
	float cx;
	float cy;
	float w;
	float h;

	// No uninitialized memory!
	Box() : cx(0.0f), cy(0.0f), w(0.0f), h(0.0f) {}
	Box(const float cx, const float cy, const float w, const float h) : cx(cx), cy(cy), w(w), h(h) {}
	Box(const float cx, const float cy, const float size) : cx(cx), cy(cy), w(size), h(size) {}
	Box(const xrt_vec2 &center, const float size) : cx(center.x), cy(center.y), w(size), h(size) {}
};

static float
overlap(float x1, float w1, float x2, float w2)
{
	float l1 = x1 - w1 / 2;
	float l2 = x2 - w2 / 2;
	float left = l1 > l2 ? l1 : l2;

	float r1 = x1 + w1 / 2;
	float r2 = x2 + w2 / 2;
	float right = r1 < r2 ? r1 : r2;

	return right - left;
}

static float
boxIntersection(const Box &a, const Box &b)
{
	float w = overlap(a.cx, a.w, b.cx, b.w);
	float h = overlap(a.cy, a.h, b.cy, b.h);

	if (w < 0 || h < 0)
		return 0;

	return w * h;
}

static float
boxUnion(const Box &a, const Box &b)
{
	return a.w * a.h + b.w * b.h - boxIntersection(a, b);
}

static float
boxIOU(const Box &a, const Box &b)
{
	return boxIntersection(a, b) / boxUnion(a, b);
}
} // namespace xrt::auxiliary::util::box_iou
