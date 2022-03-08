// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Helper math to do things with images for the camera-based hand tracker
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */
#pragma once

#include "math/m_vec2.h"
#include "math/m_vec3.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/core/types.hpp>

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

struct xrt_vec3
raycoord(struct ht_view *htv, struct xrt_vec3 model_out)
{
	cv::Mat in_px_coords(1, 1, CV_32FC2);
	float *write_in;
	write_in = in_px_coords.ptr<float>(0);
	write_in[0] = model_out.x;
	write_in[1] = model_out.y;
	cv::Mat out_ray(1, 1, CV_32FC2);

	cv::fisheye::undistortPoints(in_px_coords, out_ray, htv->cameraMatrix, htv->distortion);


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
	return o;
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

void
centerAndRotationFromJoints(struct ht_view *htv,
                            const xrt_vec2 *wrist,
                            const xrt_vec2 *index,
                            const xrt_vec2 *middle,
                            const xrt_vec2 *little,
                            xrt_vec2 *out_center,
                            xrt_vec2 *out_wrist_to_middle)
{
	// Close to what Mediapipe does, but slightly different - just uses the middle proximal instead of "estimating"
	// it from the pinky and index.
	// at the end of the day I should probably do that basis vector filtering thing to get a nicer middle metacarpal
	// from 6 keypoints (not thumb proximal) OR SHOULD I. because distortion. hmm

	// Feel free to look at the way MP does it, you can see it's different.
	// https://github.com/google/mediapipe/blob/master/mediapipe/modules/holistic_landmark/calculators/hand_detections_from_pose_to_rects_calculator.cc

	// struct xrt_vec2 hand_center = m_vec2_mul_scalar(middle, 0.5) + m_vec2_mul_scalar(index, 0.5*(2.0f/3.0f)) +
	// m_vec2_mul_scalar(little, 0.5f*((1.0f/3.0f))); // Middle proximal, straight-up.
	// U_LOG_E("%f %f  %f %f  %f %f  %f %f  ", wrist.x, wrist.y, index.x, index.y, middle.x, middle.y, little.x,
	// little.y);
	*out_center = m_vec2_lerp(*middle, m_vec2_lerp(*index, *little, 1.0f / 3.0f), 0.25f);

	*out_wrist_to_middle = *out_center - *wrist;
}

struct DetectionModelOutput
rotatedRectFromJoints(struct ht_view *htv, xrt_vec2 center, xrt_vec2 wrist_to_middle, DetectionModelOutput *out)
{
	float box_size = m_vec2_len(wrist_to_middle) * 2.0f * 1.73f;

	double rot = atan2(wrist_to_middle.x, wrist_to_middle.y) * (-180.0f / M_PI);

	out->rotation = rot;
	out->size = box_size;
	out->center = center;

	cv::RotatedRect rrect =
	    cv::RotatedRect(cv::Point2f(out->center.x, out->center.y), cv::Size2f(out->size, out->size), out->rotation);


	cv::Point2f vertices[4];
	rrect.points(vertices);
	if (htv->htd->debug_scribble && htv->htd->dynamic_config.scribble_bounding_box) {
		for (int i = 0; i < 4; i++) {
			cv::Scalar b = cv::Scalar(10, 30, 30);
			if (i == 3) {
				b = cv::Scalar(255, 255, 0);
			}
			cv::line(htv->debug_out_to_this, vertices[i], vertices[(i + 1) % 4], b, 2);
		}
	}
	// topright is 0. bottomright is 1. bottomleft is 2. topleft is 3.

	cv::Point2f src_tri[3] = {vertices[3], vertices[2], vertices[1]}; // top-left, bottom-left, bottom-right

	cv::Point2f dest_tri[3] = {cv::Point2f(0, 0), cv::Point2f(0, 224), cv::Point2f(224, 224)};

	out->warp_there = getAffineTransform(src_tri, dest_tri);
	out->warp_back = getAffineTransform(dest_tri, src_tri);

	// out->wrist = wrist;

	return *out;
}

void
planarize(const cv::Mat &input, uint8_t *output)
{
	// output better be the right size, because we are not doing any bounds checking!
	assert(input.isContinuous());
	int lix = input.cols;
	int liy = input.rows;
	cv::Mat planes[3];
	cv::split(input, planes);
	cv::Mat red = planes[0];
	cv::Mat green = planes[1];
	cv::Mat blue = planes[2];
	memcpy(output, red.data, lix * liy);
	memcpy(output + (lix * liy), green.data, lix * liy);
	memcpy(output + (lix * liy * 2), blue.data, lix * liy);
}
