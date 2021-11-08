// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Helper math to do things with images for the camera-based hand tracker
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

#include "math/m_vec3.h"

#include "ht_driver.hpp"

#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

struct ht_view;

cv::Scalar
hsv2rgb(float fH, float fS, float fV);

struct xrt_vec3
raycoord(struct ht_view *htv, struct xrt_vec3 model_out);

/*!
 * Returns a 2x3 transform matrix that takes you back from the blackbarred image to the original image.
 */
cv::Matx23f
blackbar(const cv::Mat &in, cv::Mat &out, xrt_size out_size);

/*!
 * This is a template so that we can use xrt_vec3 or xrt_vec2.
 * Please don't use this for anything other than xrt_vec3 or xrt_vec2!
 */
template <typename T>
T
transformVecBy2x3(T in, cv::Matx23f warp_back)
{
	T rr;
	rr.x = (in.x * warp_back(0, 0)) + (in.y * warp_back(0, 1)) + warp_back(0, 2);
	rr.y = (in.x * warp_back(1, 0)) + (in.y * warp_back(1, 1)) + warp_back(1, 2);
	return rr;
}

//! Draw some dots. Factors out some boilerplate.
void
handDot(cv::Mat &mat, xrt_vec2 place, float radius, float hue, float intensity, int type);

void
centerAndRotationFromJoints(struct ht_view *htv,
                            const xrt_vec2 *wrist,
                            const xrt_vec2 *index,
                            const xrt_vec2 *middle,
                            const xrt_vec2 *little,
                            xrt_vec2 *out_center,
                            xrt_vec2 *out_wrist_to_middle);

struct DetectionModelOutput
rotatedRectFromJoints(struct ht_view *htv, xrt_vec2 center, xrt_vec2 wrist_to_middle, DetectionModelOutput *out);

void
planarize(const cv::Mat &input, uint8_t *output);
