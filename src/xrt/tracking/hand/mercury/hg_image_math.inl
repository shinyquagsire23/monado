// Copyright 2021-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Helper header for drawing and image transforms
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */
#pragma once

#include "hg_sync.hpp"

namespace xrt::tracking::hand::mercury {

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

static cv::Scalar
hsv2rgb(float fH, float fS, float fV)
{
	const float fC = fV * fS; // Chroma
	const float fHPrime = fmod(fH / 60.0, 6);
	const float fX = fC * (1 - fabs(fmod(fHPrime, 2) - 1));
	const float fM = fV - fC;

	float fR, fG, fB;

	if (0 <= fHPrime && fHPrime < 1) {
		fR = fC;
		fG = fX;
		fB = 0;
	} else if (1 <= fHPrime && fHPrime < 2) {
		fR = fX;
		fG = fC;
		fB = 0;
	} else if (2 <= fHPrime && fHPrime < 3) {
		fR = 0;
		fG = fC;
		fB = fX;
	} else if (3 <= fHPrime && fHPrime < 4) {
		fR = 0;
		fG = fX;
		fB = fC;
	} else if (4 <= fHPrime && fHPrime < 5) {
		fR = fX;
		fG = 0;
		fB = fC;
	} else if (5 <= fHPrime && fHPrime < 6) {
		fR = fC;
		fG = 0;
		fB = fX;
	} else {
		fR = 0;
		fG = 0;
		fB = 0;
	}

	fR += fM;
	fG += fM;
	fB += fM;
	return {fR * 255.0f, fG * 255.0f, fB * 255.0f};
}


//! @optimize Make it take an array of vec2's and give out an array of vec2's, then
// put it in its own target so we don't have to link to OpenCV.
// Oooorrrrr... actually add good undistortion stuff to Monado so that we don't have to depend on OpenCV at all.

XRT_MAYBE_UNUSED static struct xrt_vec2
raycoord(ht_view *htv, struct xrt_vec2 model_out)
{
	model_out.x *= htv->hgt->multiply_px_coord_for_undistort;
	model_out.y *= htv->hgt->multiply_px_coord_for_undistort;
	cv::Mat in_px_coords(1, 1, CV_32FC2);
	float *write_in;
	write_in = in_px_coords.ptr<float>(0);
	write_in[0] = model_out.x;
	write_in[1] = model_out.y;
	cv::Mat out_ray(1, 1, CV_32FC2);

	if (htv->hgt->use_fisheye) {
		cv::fisheye::undistortPoints(in_px_coords, out_ray, htv->cameraMatrix, htv->distortion);
	} else {
		cv::undistortPoints(in_px_coords, out_ray, htv->cameraMatrix, htv->distortion);
	}

	float n_x = out_ray.at<float>(0, 0);
	float n_y = out_ray.at<float>(0, 1);

	return {n_x, n_y};
}

static void
handDot(cv::Mat &mat, xrt_vec2 place, float radius, float hue, float intensity, int type)
{
	cv::circle(mat, {(int)place.x, (int)place.y}, radius, hsv2rgb(hue * 360.0f, intensity, intensity), type);
}
} // namespace xrt::tracking::hand::mercury
