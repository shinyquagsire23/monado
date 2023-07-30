// Copyright 2021-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Utility to do batch stereographic projections of images.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */

#include <cmath>
#include <opencv2/core.hpp>
#include <stdio.h>

#include <string>
#include "math/m_vec3.h"
#include "math/m_vec2.h"

#include "util/u_time.h"

#include "xrt/xrt_defines.h"
#include "math/m_space.h"
#include <filesystem>
#include <fstream>
#include "os/os_time.h"
#include "util/u_logging.h"
#include "tracking/t_tracking.h"

#include "tracking/t_calibration_opencv.hpp"
#include <iostream>
#include <opencv2/opencv.hpp>

#include "math/m_eigen_interop.hpp"
#include "hg_sync.hpp"
#include "hg_stereographic_unprojection.hpp"

namespace xrt::tracking::hand::mercury {

constexpr int wsize = 128;

template <typename T> using OutputSizedArray = Eigen::Array<T, wsize, wsize, Eigen::RowMajor>;
using OutputSizedFloatArray = OutputSizedArray<float>;

#define ARRAY_STACK_SIZE 20

struct ArrayStack
{
	OutputSizedFloatArray arrays[ARRAY_STACK_SIZE];
	size_t array_idx = 0;

	OutputSizedFloatArray &
	get()
	{
		if (array_idx == ARRAY_STACK_SIZE) {
			abort();
		}
		return this->arrays[this->array_idx++];
	};

	void
	dropAll()
	{
		this->array_idx = 0;
	}
};

struct projection_state
{
	cv::Mat &input;
	Eigen::Map<OutputSizedArray<uint8_t>> distorted_image_eigen;
	t_camera_model_params dist;

	const projection_instructions &instructions;

	ArrayStack stack = {};

	OutputSizedArray<int16_t> image_x = {};
	OutputSizedArray<int16_t> image_y = {};

	projection_state(const projection_instructions &instructions, cv::Mat &input, cv::Mat &output)
	    : input(input), distorted_image_eigen(output.data, 128, 128), instructions(instructions){};
};


// A private, purpose-optimized version of the Kannalla-Brandt projection function.
static void
project_kb4(projection_state &mi,
            const OutputSizedFloatArray &x, //
            const OutputSizedFloatArray &y, //
            const OutputSizedFloatArray &z, //
            OutputSizedFloatArray &out_x,   //
            OutputSizedFloatArray &out_y)
{
	const t_camera_model_params &dist = mi.dist;
	OutputSizedFloatArray r2 = mi.stack.get();
	OutputSizedFloatArray r = mi.stack.get();

	r2 = x * x + y * y;
	r = sqrt(r2);

#if 0
		const I theta = atan2(r, z);
#else
	// This works here but will not work in eg. a nonlinear optimizer, or for more general applications.
	// Takes about 200us off the runtime.
	// Basically:
	// * We can be sure that z won't be negative only because previous hand tracking code checks this for
	// us.
	// * x,y,z is normalized so we don't have to worry about numerical stability.
	// If neither of these were true we'd definitely need atan2.
	//
	// Grrr, we really need a good library for fast approximations of trigonometric functions.
	OutputSizedFloatArray theta = mi.stack.get();
	theta = atan(r / z);
#endif

	OutputSizedFloatArray theta2 = mi.stack.get();
	theta2 = theta * theta;


#if 0
		I r_theta = dist.fisheye.k4 * theta2;
		r_theta += dist.fisheye.k3;
		r_theta *= theta2;
		r_theta += dist.fisheye.k2;
		r_theta *= theta2;
		r_theta += dist.fisheye.k1;
		r_theta *= theta2;
		r_theta += 1;
		r_theta *= theta;
#else
	// This version gives the compiler more options to do FMAs and avoid temporaries. Down to floating point
	// precision this should give the same result as the above.
	OutputSizedFloatArray r_theta = mi.stack.get();
	r_theta =
	    (((((dist.fisheye.k4 * theta2) + dist.fisheye.k3) * theta2 + dist.fisheye.k2) * theta2 + dist.fisheye.k1) *
	         theta2 +
	     1) *
	    theta;
#endif

	OutputSizedFloatArray mx = mi.stack.get();
	mx = x * r_theta / r;
	OutputSizedFloatArray my = mi.stack.get();
	my = y * r_theta / r;

	out_x = dist.fx * mx + dist.cx;
	out_y = dist.fy * my + dist.cy;
}

template <typename T>
T
map_ranges(T value, T from_low, T from_high, T to_low, T to_high)
{
	return (value - from_low) * (to_high - to_low) / (from_high - from_low) + to_low;
}

void
naive_remap(OutputSizedArray<int16_t> &image_x,
            OutputSizedArray<int16_t> &image_y,
            cv::Mat &input,
            Eigen::Map<OutputSizedArray<uint8_t>> &output)
{
	output = 0;

	for (int y = 0; y < wsize; y++) {
		for (int x = 0; x < wsize; x++) {
			if (image_y(y, x) < 0) {
				continue;
			}
			if (image_y(y, x) >= input.rows) {
				continue;
			}
			if (image_x(y, x) < 0) {
				continue;
			}
			if (image_x(y, x) >= input.cols) {
				continue;
			}
			output(y, x) = input.at<uint8_t>(image_y(y, x), image_x(y, x));
		}
	}
}



void
StereographicDistort(projection_state &mi)
{
	XRT_TRACE_MARKER();

	OutputSizedFloatArray &sg_x = mi.stack.get();
	OutputSizedFloatArray &sg_y = mi.stack.get();

	// Please vectorize me?
	if (mi.instructions.flip) {
		for (int x = 0; x < wsize; ++x) {
			sg_x.col(x).setConstant(map_ranges<float>((float)x, 0.0f, (float)wsize,
			                                          (float)mi.instructions.stereographic_radius,
			                                          (float)-mi.instructions.stereographic_radius));
		}
	} else {
		for (int x = 0; x < wsize; ++x) {
			sg_x.col(x).setConstant(map_ranges<float>((float)x, 0.0f, (float)wsize,
			                                          (float)-mi.instructions.stereographic_radius,
			                                          (float)mi.instructions.stereographic_radius));
		}
	}
	// Ditto?
	for (int y = 0; y < wsize; ++y) {
		sg_y.row(y).setConstant(map_ranges<float>((float)y, 0.0f, (float)wsize,
		                                          (float)mi.instructions.stereographic_radius,
		                                          (float)-mi.instructions.stereographic_radius));
	}


	// STEREOGRAPHIC DIRECTION TO 3D DIRECTION
	// Note: we do not normalize the direction, because we don't need to. :)

	OutputSizedFloatArray &dir_x = mi.stack.get();
	OutputSizedFloatArray &dir_y = mi.stack.get();
	OutputSizedFloatArray &dir_z = mi.stack.get();


#if 0
	dir_x = sg_x * 2;
	dir_y = sg_y * 2;
#else
	// Adding something to itself is faster than multiplying itself by 2
	// and unless you have fast-math the compiler won't do it for you. =/
	dir_x = sg_x + sg_x;
	dir_y = sg_y + sg_y;
#endif

	dir_z = (sg_x * sg_x) + (sg_y * sg_y) - 1;
	// END STEREOGRAPHIC DIRECTION TO 3D DIRECTION

	// QUATERNION ROTATING VECTOR
	OutputSizedFloatArray &rot_dir_x = mi.stack.get();
	OutputSizedFloatArray &rot_dir_y = mi.stack.get();
	OutputSizedFloatArray &rot_dir_z = mi.stack.get();

	OutputSizedFloatArray &uv0 = mi.stack.get();
	OutputSizedFloatArray &uv1 = mi.stack.get();
	OutputSizedFloatArray &uv2 = mi.stack.get();

	const Eigen::Quaternionf &q = mi.instructions.rot_quat;

	uv0 = q.y() * dir_z - q.z() * dir_y;
	uv1 = q.z() * dir_x - q.x() * dir_z;
	uv2 = q.x() * dir_y - q.y() * dir_x;

	uv0 += uv0;
	uv1 += uv1;
	uv2 += uv2;

	rot_dir_x = dir_x + q.w() * uv0;
	rot_dir_y = dir_y + q.w() * uv1;
	rot_dir_z = dir_z + q.w() * uv2;

	rot_dir_x += q.y() * uv2 - q.z() * uv1;
	rot_dir_y += q.z() * uv0 - q.x() * uv2;
	rot_dir_z += q.x() * uv1 - q.y() * uv0;
	// END QUATERNION ROTATING VECTOR



	OutputSizedFloatArray &image_x_f = mi.stack.get();
	OutputSizedFloatArray &image_y_f = mi.stack.get();


	//!@todo optimize
	rot_dir_y *= -1;
	rot_dir_z *= -1;

	{
		XRT_TRACE_IDENT(camera_projection);

		switch (mi.dist.model) {
		case T_DISTORTION_FISHEYE_KB4:
			// This takes 250us vs 500 because of the removed atan2.
			project_kb4(mi, rot_dir_x, rot_dir_y, rot_dir_z, image_x_f, image_y_f);
			break;
		case T_DISTORTION_OPENCV_RADTAN_8:
			// Regular C is plenty fast for radtan :)
			for (int i = 0; i < image_x_f.rows(); i++) {
				for (int j = 0; j < image_x_f.cols(); j++) {
					t_camera_models_project(&mi.dist, rot_dir_x(i, j), rot_dir_y(i, j),
					                        rot_dir_z(i, j), &image_x_f(i, j), &image_y_f(i, j));
				}
			}
			break;
		default: assert(false);
		}
	}

	mi.image_x = image_x_f.cast<int16_t>();
	mi.image_y = image_y_f.cast<int16_t>();

	naive_remap(mi.image_x, mi.image_y, mi.input, mi.distorted_image_eigen);
}



bool
slow(projection_state &mi, float x, float y, cv::Point2i &out)
{
	float sg_x =
	    map_ranges<float>(x, 0, wsize, -mi.instructions.stereographic_radius, mi.instructions.stereographic_radius);

	float sg_y =
	    map_ranges<float>(y, 0, wsize, mi.instructions.stereographic_radius, -mi.instructions.stereographic_radius);

	Eigen::Vector3f dir = stereographic_unprojection(sg_x, sg_y);

	dir = mi.instructions.rot_quat * dir;

	dir.y() *= -1;
	dir.z() *= -1;

	float _x = {};
	float _y = {};

	bool ret = t_camera_models_project(&mi.dist, dir.x(), dir.y(), dir.z(), &_x, &_y);

	out.x = _x;
	out.y = _y;

	return ret;
}

void
draw_and_clear(cv::Mat img, std::vector<cv::Point> &line_vec, bool good, cv::Scalar color)
{
	if (!good) {
		color[0] = 255 - color[0];
		color[1] = 255 - color[1];
		color[2] = 255 - color[2];
	}
	cv::polylines(img, line_vec, false, color);
	line_vec.clear();
}

void
add_or_draw_line(projection_state &mi,             //
                 int x,                            //
                 int y,                            //
                 std::vector<cv::Point> &line_vec, //
                 cv::Scalar color,                 //
                 bool &good_most_recent,           //
                 bool &started,
                 cv::Mat &img)
{
	cv::Point2i e = {};
	bool retval = slow(mi, x, y, e);

	if (!started) {
		started = true;
		good_most_recent = retval;
		line_vec.push_back(e);
		return;
	}
	if (retval != good_most_recent) {
		line_vec.push_back(e);
		draw_and_clear(img, line_vec, good_most_recent, color);
	}
	good_most_recent = retval;
	line_vec.push_back(e);
}

void
draw_boundary(projection_state &mi, cv::Scalar color, cv::Mat img)
{
	std::vector<cv::Point> line_vec = {};
	bool good_most_recent = true;
	bool started = false;

	constexpr float step = 16;

	// x = 0, y = 0->128
	for (int y = 0; y <= wsize; y += step) {
		int x = 0;
		add_or_draw_line(mi, x, y, line_vec, color, good_most_recent, started, img);
	}

	// x = 0->128, y = 128
	for (int x = step; x <= wsize; x += step) {
		int y = wsize;
		add_or_draw_line(mi, x, y, line_vec, color, good_most_recent, started, img);
	}

	// x = 128, y = 128->0
	for (int y = wsize - step; y >= 0; y -= step) {
		int x = wsize;
		add_or_draw_line(mi, x, y, line_vec, color, good_most_recent, started, img);
	}

	// x = 128->0, y = 0
	for (int x = wsize - step; x >= 0; x -= step) {
		int y = 0;
		add_or_draw_line(mi, x, y, line_vec, color, good_most_recent, started, img);
	}

	draw_and_clear(img, line_vec, good_most_recent, color);
}

void
project_21_points_unscaled(Eigen::Array<float, 3, 21> &joints_local, Eigen::Quaternionf rot_quat, hand21_2d &out_joints)
{
	for (int i = 0; i < 21; i++) {
		Eigen::Vector3f direction = joints_local.col(i); //{d.x, d.y, d.z};
		direction.normalize();

		direction = rot_quat.conjugate() * direction;
		float denom = 1 - direction.z();
		float sg_x = direction.x() / denom;
		float sg_y = direction.y() / denom;
		// sg_x *= mi.stereographic_radius;
		// sg_y *= mi.stereographic_radius;

		out_joints[i].pos_2d.x = sg_x;
		out_joints[i].pos_2d.y = sg_y;
	}
}

template <typename V2>
void
project_point_scaled(projection_state &mi, Eigen::Vector3f direction, V2 &out_img_pt)
{
	direction = mi.instructions.rot_quat.conjugate() * direction;
	float denom = 1 - direction.z();
	float sg_x = direction.x() / denom;
	float sg_y = direction.y() / denom;

	out_img_pt.pos_2d.x = map_ranges<float>(sg_x, -mi.instructions.stereographic_radius,
	                                        mi.instructions.stereographic_radius, 0, wsize);
	out_img_pt.pos_2d.y = map_ranges<float>(sg_y, mi.instructions.stereographic_radius,
	                                        -mi.instructions.stereographic_radius, 0, wsize);
}

void
project_21_points_scaled(projection_state &mi, Eigen::Array<float, 3, 21> &joints_local, hand21_2d &out_joints_in_img)
{
	for (int i = 0; i < 21; i++) {
		project_point_scaled(mi, Eigen::Vector3f(joints_local.col(i)), out_joints_in_img[i]);
	}
}



Eigen::Quaternionf
direction(Eigen::Vector3f dir, float twist)
{
	Eigen::Quaternionf one = Eigen::Quaternionf().setFromTwoVectors(-Eigen::Vector3f::UnitZ(), dir);

	Eigen::Quaternionf two;
	two = Eigen::AngleAxisf(twist, -Eigen::Vector3f::UnitZ());
	return one * two;
}

void
add_rel_depth(const Eigen::Array<float, 3, 21> &joints, hand21_2d &out_joints_in_img)
{
	float hand_size = (Eigen::Vector3f(joints.col(0)) - Eigen::Vector3f(joints.col(9))).norm();

	float midpxm_depth = Eigen::Vector3f(joints.col(9)).norm();
	for (int i = 0; i < 21; i++) {
		float jd = Eigen::Vector3f(joints.col(i)).norm();
		out_joints_in_img[i].depth_relative_to_midpxm = (jd - midpxm_depth) / hand_size;
	}
}

static float
palm_length(hand21_2d &joints)
{
	vec2_5 wrist = joints[0];
	vec2_5 middle_proximal = joints[9];

	vec2_5 index_proximal = joints[5];
	vec2_5 ring_proximal = joints[17];

	float fwd = m_vec2_len(wrist.pos_2d - middle_proximal.pos_2d);
	float side = m_vec2_len(index_proximal.pos_2d - ring_proximal.pos_2d);

	float length = fmaxf(fwd, side);

	return length;
}



void
make_projection_instructions(t_camera_model_params &dist,
                             bool flip_after,
                             float expand_val,
                             float twist,
                             Eigen::Array<float, 3, 21> &joints,
                             projection_instructions &out_instructions,
                             hand21_2d &out_hand)
{

	out_instructions.flip = flip_after;

	Eigen::Vector3f dir = Eigen::Vector3f(joints.col(9)).normalized();

	out_instructions.rot_quat = direction(dir, twist);

	Eigen::Vector3f old_direction = dir;


	// Tested on Dec 7: This converges in 4 iterations max, usually 2.
	for (int i = 0; i < 8; i++) {
		project_21_points_unscaled(joints, out_instructions.rot_quat, out_hand);

		xrt_vec2 min = out_hand[0].pos_2d;
		xrt_vec2 max = out_hand[0].pos_2d;

		for (int i = 0; i < 21; i++) {
			xrt_vec2 pt = out_hand[i].pos_2d;
			min.x = fmin(pt.x, min.x);
			min.y = fmin(pt.y, min.y);

			max.x = fmax(pt.x, max.x);
			max.y = fmax(pt.y, max.y);
		}


		xrt_vec2 center = m_vec2_mul_scalar(min + max, 0.5);

		float r = fmax(center.x - min.x, center.y - min.y);
		out_instructions.stereographic_radius = r;

		Eigen::Vector3f new_direction = stereographic_unprojection(center.x, center.y);

		new_direction = out_instructions.rot_quat * new_direction;

		out_instructions.rot_quat = direction(new_direction, twist);


		if ((old_direction - dir).norm() < 0.0001) {
			// We converged
			break;
		}
		old_direction = dir;
	}

	// This can basically be removed (we will have converged very well in the above), but for correctness's
	// sake, let's project one last time.
	project_21_points_unscaled(joints, out_instructions.rot_quat, out_hand);

	// These are to ensure that the bounding box doesn't get too small around a closed fist.
	float palm_l = palm_length(out_hand);
	float radius_around_palm = palm_l * 0.5 * (2.2 / 1.65) * expand_val;

	out_instructions.stereographic_radius *= expand_val;

	out_instructions.stereographic_radius = fmaxf(out_instructions.stereographic_radius, radius_around_palm);

	// This is going straight into the (-1, 1)-normalized space
	for (int i = 0; i < 21; i++) {
		vec2_5 &v = out_hand[i];
		v.pos_2d.x = map_ranges<float>(v.pos_2d.x, -out_instructions.stereographic_radius,
		                               out_instructions.stereographic_radius, -1, 1);
		//!@todo optimize
		if (flip_after) {
			v.pos_2d.x *= -1;
		}
		//!@todo this is probably wrong, should probably be negated
		v.pos_2d.y = map_ranges<float>(v.pos_2d.y, out_instructions.stereographic_radius,
		                               -out_instructions.stereographic_radius, -1, 1);
	}
	add_rel_depth(joints, out_hand);
}

void
make_projection_instructions_angular(xrt_vec3 direction_3d,
                                     bool flip_after,
                                     float angular_radius,
                                     float expand_val,
                                     float twist,
                                     projection_instructions &out_instructions)
{

	out_instructions.flip = flip_after;

	xrt_vec3 imaginary_direction = {0, sinf(angular_radius), -cosf(angular_radius)};

	out_instructions.stereographic_radius = imaginary_direction.y / (1 - imaginary_direction.z);

	math_vec3_normalize(&direction_3d);

	Eigen::Vector3f dir = xrt::auxiliary::math::map_vec3(direction_3d);


	out_instructions.rot_quat = direction(dir, twist);


	out_instructions.stereographic_radius *= expand_val;
}


void
stereographic_project_image(const t_camera_model_params &dist,
                            const projection_instructions &instructions,
                            cv::Mat &input_image,
                            cv::Mat *debug_image,
                            const cv::Scalar boundary_color,
                            cv::Mat &out)

{
	out = cv::Mat(cv::Size(wsize, wsize), CV_8U);
	projection_state *mi_ptr = new projection_state(instructions, input_image, out);
	projection_state &mi = *mi_ptr;

	mi.dist = dist;

	StereographicDistort(mi);

	if (debug_image) {
		draw_boundary(mi, boundary_color, *debug_image);
	}
	delete mi_ptr;
}
} // namespace xrt::tracking::hand::mercury
