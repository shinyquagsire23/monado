// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to deal with bounding boxes for camera-based hand-tracking.
 * @author Moses Turner <moses@collabora.com>
 * @author Marcus Edel <marcus.edel@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

#include "xrt/xrt_defines.h"

#include <vector>

struct Box
{
	float cx;
	float cy;
	float w;
	float h;
};

struct NMSPalm
{
	Box bbox;
	struct xrt_vec2 keypoints[7];
	float confidence;
};

std::vector<NMSPalm>
filterBoxesWeightedAvg(const std::vector<NMSPalm> &detections, float min_iou = 0.1f);
