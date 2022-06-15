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

cv::Scalar
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

struct xrt_vec2
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


	struct xrt_vec3 n = {n_x, n_y, 1.0f};

	cv::Matx33f R = htv->rotate_camera_to_stereo_camera;

	struct xrt_vec3 o = {
	    (n.x * R(0, 0)) + (n.y * R(0, 1)) + (n.z * R(0, 2)),
	    (n.x * R(1, 0)) + (n.y * R(1, 1)) + (n.z * R(1, 2)),
	    (n.x * R(2, 0)) + (n.y * R(2, 1)) + (n.z * R(2, 2)),
	};

	math_vec3_scalar_mul(1.0f / o.z, &o);
	return {o.x, o.y};
}

cv::Matx23f
blackbar(const cv::Mat &in, cv::Mat &out, xrt_size out_size)
{
#if 1
	// Easy to think about, always right, but pretty slow:
	// Get a matrix from the original to the scaled down / blackbar'd image, then get one that goes back.
	// Then just warpAffine() it.
	// Easy in programmer time - never have to worry about off by one, special cases. We can come back and optimize
	// later.

	// Do the black bars need to be on top and bottom, or on left and right?
	float scale_down_w = (float)out_size.w / (float)in.cols; // 128/1280 = 0.1
	float scale_down_h = (float)out_size.h / (float)in.rows; // 128/800 =  0.16

	float scale_down = fmin(scale_down_w, scale_down_h); // 0.1

	float width_inside = (float)in.cols * scale_down;
	float height_inside = (float)in.rows * scale_down;

	float translate_x = (out_size.w - width_inside) / 2;  // should be 0 for 1280x800
	float translate_y = (out_size.h - height_inside) / 2; // should be (1280-800)/2 = 240

	cv::Matx23f go;
	// clang-format off
	go(0,0) = scale_down;  go(0,1) = 0.0f;                  go(0,2) = translate_x;
	go(1,0) = 0.0f;                  go(1,1) = scale_down;  go(1,2) = translate_y;
	// clang-format on

	cv::warpAffine(in, out, go, cv::Size(out_size.w, out_size.h));

	cv::Matx23f ret;

	// clang-format off
	ret(0,0) = 1.0f/scale_down;  ret(0,1) = 0.0f;             ret(0,2) = -translate_x/scale_down;
	ret(1,0) = 0.0f;             ret(1,1) = 1.0f/scale_down;  ret(1,2) = -translate_y/scale_down;
	// clang-format on

	return ret;
#else
	// Fast, always wrong if the input isn't square. You'd end up using something like this, plus some
	// copyMakeBorder if you want to optimize.
	if (aspect_ratio_input == aspect_ratio_output) {
		cv::resize(in, out, {out_size.w, out_size.h});
		cv::Matx23f ret;
		float scale_from_out_to_in = (float)in.cols / (float)out_size.w;
		// clang-format off
		ret(0,0) = scale_from_out_to_in;  ret(0,1) = 0.0f;                  ret(0,2) = 0.0f;
		ret(1,0) = 0.0f;                  ret(1,1) = scale_from_out_to_in;  ret(1,2) = 0.0f;
		// clang-format on
		cv::imshow("hi", out);
		cv::waitKey(1);
		return ret;
	}
	assert(!"Uh oh! Unimplemented!");
	return {};
#endif
}

void
handDot(cv::Mat &mat, xrt_vec2 place, float radius, float hue, float intensity, int type)
{
	cv::circle(mat, {(int)place.x, (int)place.y}, radius, hsv2rgb(hue * 360.0f, intensity, intensity), type);
}
} // namespace xrt::tracking::hand::mercury
