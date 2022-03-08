// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to deal with bounding boxes for camera-based hand-tracking.
 * @author Moses Turner <moses@collabora.com>
 * @author Marcus Edel <marcus.edel@collabora.com>
 * @ingroup drv_ht
 */

#include "rgb_sync.hpp"
#include <math.h>

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

static NMSPalm
weightedAvgBoxes(const std::vector<NMSPalm> &detections)
{
	float weight = 0.0f; // or, sum_confidences.
	float cx = 0.0f;
	float cy = 0.0f;
	float size = 0.0f;
	NMSPalm out = {};

	for (const NMSPalm &detection : detections) {
		weight += detection.confidence;
		cx += detection.bbox.cx * detection.confidence;
		cy += detection.bbox.cy * detection.confidence;
		size += detection.bbox.w * .5 * detection.confidence;
		size += detection.bbox.h * .5 * detection.confidence;

		for (int i = 0; i < 7; i++) {
			out.keypoints[i].x += detection.keypoints[i].x * detection.confidence;
			out.keypoints[i].y += detection.keypoints[i].y * detection.confidence;
		}
	}
	cx /= weight;
	cy /= weight;
	size /= weight;
	for (int i = 0; i < 7; i++) {
		out.keypoints[i].x /= weight;
		out.keypoints[i].y /= weight;
	}


	float bare_confidence = weight / detections.size();

	// desmos \frac{1}{1+e^{-.5x}}-.5

	float steep = 0.2;
	float cent = 0.5;

	float exp = detections.size();

	float sigmoid_addendum = (1.0f / (1.0f + pow(M_E, (-steep * exp)))) - cent;

	float diff_bare_to_one = 1.0f - bare_confidence;

	out.confidence = bare_confidence + (sigmoid_addendum * diff_bare_to_one);

	// U_LOG_E("Bare %f num %f sig %f diff %f out %f", bare_confidence, exp, sigmoid_addendum, diff_bare_to_one,
	// out.confidence);

	out.bbox.cx = cx;
	out.bbox.cy = cy;
	out.bbox.w = size;
	out.bbox.h = size;
	return out;
}

std::vector<NMSPalm>
filterBoxesWeightedAvg(const std::vector<NMSPalm> &detections, float min_iou)
{
	std::vector<std::vector<NMSPalm>> overlaps;
	std::vector<NMSPalm> outs;

	// U_LOG_D("\n\nStarting filtering boxes. There are %zu boxes to look at.\n", detections.size());
	for (const NMSPalm &detection : detections) {
		// U_LOG_D("Starting looking at one detection\n");
		bool foundAHome = false;
		for (size_t i = 0; i < outs.size(); i++) {
			float iou = boxIOU(outs[i].bbox, detection.bbox);
			// U_LOG_D("IOU is %f\n", iou);
			// U_LOG_D("Outs box is %f %f %f %f", outs[i].bbox.cx, outs[i].bbox.cy, outs[i].bbox.w,
			// outs[i].bbox.h)
			if (iou > min_iou) {
				// This one intersects with the whole thing
				overlaps[i].push_back(detection);
				outs[i] = weightedAvgBoxes(overlaps[i]);
				foundAHome = true;
				break;
			}
		}
		if (!foundAHome) {
			// U_LOG_D("No home\n");
			overlaps.push_back({detection});
			outs.push_back({detection});
		} else {
			// U_LOG_D("Found a home!\n");
		}
	}
	// U_LOG_D("Sizeeeeeeeeeeeeeeeeeeeee is %zu\n", outs.size());
	return outs;
}
