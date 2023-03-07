// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Levenberg-Marquardt kinematic optimizer
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */

#include "math/m_api.h"
#include "math/m_vec3.h"
#include "math/m_eigen_interop.hpp"
#include "os/os_time.h"
#include "util/u_logging.h"
#include "util/u_misc.h"
#include "util/u_trace_marker.h"

#include "tinyceres/tiny_solver.hpp"
#include "tinyceres/tiny_solver_autodiff_function.hpp"
#include "lm_rotations.inl"

#include <iostream>
#include <cmath>
#include <random>
#include "lm_interface.hpp"
#include "lm_optimizer_params_packer.inl"
#include "lm_defines.hpp"

#include "../hg_numerics_checker.hpp"
#include "../hg_stereographic_unprojection.hpp"

namespace xrt::tracking::hand::mercury::lm {
using namespace xrt::auxiliary::math;

// Generated from mercury_train's hand_depth_guess.ipynb.
float
sympy_guess_distance(float angle, float wrist_extra_distance_meters, float hand_size)
{
	float d1 = (1.0 / 2.0) *
	           (-wrist_extra_distance_meters * (angle - 1) +
	            sqrt((angle - 1) * (angle * pow(wrist_extra_distance_meters, 2) - 2 * pow(hand_size, 2) +
	                                pow(wrist_extra_distance_meters, 2)))) /
	           (angle - 1);
	float d2 = -(wrist_extra_distance_meters * (angle - 1) +
	             sqrt((angle - 1) * (angle * pow(wrist_extra_distance_meters, 2) - 2 * pow(hand_size, 2) +
	                                 pow(wrist_extra_distance_meters, 2)))) /
	           (2 * angle - 2);

	if (d1 > d2) {
		return d1;
	}
	return d2;
}

bool
hand_init_guess(one_frame_input &observation, const float hand_size, xrt_pose left_in_right, xrt_pose &out_wrist_guess)
{
	int num_observation_views = 0;


	// We're actually going "forwards" in our transformations for once!
	// For the right camera, instead of moving an estimation *into* right-camera-space and doing math in
	// camera-space, we move everything into *left-camera-space*.
	xrt_pose right_in_left;
	math_pose_invert(&left_in_right, &right_in_left);

	xrt_vec3 wrist_global_sum = {};
	xrt_vec3 midpxm_global_sum = {};
	xrt_vec3 indpxm_global_sum = {};
	xrt_vec3 litpxm_global_sum = {};

	for (int view = 0; view < 2; view++) {
		one_frame_one_view &v = observation.views[view];
		if (!v.active) {
			continue;
		}
		num_observation_views++;

		Eigen::Quaternionf rotate = map_quat(v.look_dir);
		Eigen::Vector3f directions_rel_camera[21];

		for (int i = 0; i < 21; i++) {
			float sg_x = v.keypoints_in_scaled_stereographic[i].pos_2d.x * v.stereographic_radius;
			float sg_y = v.keypoints_in_scaled_stereographic[i].pos_2d.y * v.stereographic_radius;
			directions_rel_camera[i] = stereographic_unprojection(sg_x, sg_y);
			directions_rel_camera[i] = rotate * directions_rel_camera[i];
		}

		xrt_vec3 midpxm_dir;
		xrt_vec3 wrist_dir;
		map_vec3(midpxm_dir) = directions_rel_camera[Joint21::MIDL_PXM];
		map_vec3(wrist_dir) = directions_rel_camera[Joint21::WRIST];

		float angle = cos(m_vec3_angle(midpxm_dir, wrist_dir));
		float wrist_extra_distance_meters =
		    v.keypoints_in_scaled_stereographic[0].depth_relative_to_midpxm * hand_size;

		float distance = sympy_guess_distance(angle, wrist_extra_distance_meters, hand_size);

		if (distance != distance) {
			// Nan check.
			// This happens if the angle between midpxm_dir and wrist_dir is 0, generally when
			// refine_center_of_distribution fails hard enough. Generally not worth tracking hands when this
			// happens.
			return false;
		}

// I wish I could make this an inline unit test. Useful for debugging so I'll leave it disabled.
#if 0
    Eigen::Vector3f wrist_rel_camera = directions_rel_camera[0] * (distance+wrist_extra_distance_meters);
    Eigen::Vector3f midpxm_rel_camera = directions_rel_camera[9] * distance;

    float norm = (wrist_rel_camera-midpxm_rel_camera).norm();
    U_LOG_E("Recovered: %f", norm);
#endif

		xrt_vec3 wrist_rel_camera = {};
		xrt_vec3 midpxm_rel_camera = {};
		xrt_vec3 indpxm_rel_camera = {};
		xrt_vec3 litpxm_rel_camera = {};

		map_vec3(wrist_rel_camera) =
		    directions_rel_camera[Joint21::WRIST] * (distance + wrist_extra_distance_meters);
		map_vec3(midpxm_rel_camera) = directions_rel_camera[Joint21::MIDL_PXM] * (distance);
		map_vec3(indpxm_rel_camera) =
		    directions_rel_camera[Joint21::INDX_PXM] *
		    (distance +
		     (v.keypoints_in_scaled_stereographic[Joint21::INDX_PXM].depth_relative_to_midpxm * hand_size));
		map_vec3(litpxm_rel_camera) =
		    directions_rel_camera[Joint21::LITL_PXM] *
		    (distance +
		     (v.keypoints_in_scaled_stereographic[Joint21::LITL_PXM].depth_relative_to_midpxm * hand_size));

		if (view == 1) {
			math_pose_transform_point(&right_in_left, &wrist_rel_camera, &wrist_rel_camera);
			math_pose_transform_point(&right_in_left, &midpxm_rel_camera, &midpxm_rel_camera);
			math_pose_transform_point(&right_in_left, &indpxm_rel_camera, &indpxm_rel_camera);
			math_pose_transform_point(&right_in_left, &litpxm_rel_camera, &litpxm_rel_camera);
		}

		wrist_global_sum += wrist_rel_camera;
		midpxm_global_sum += midpxm_rel_camera;
		indpxm_global_sum += indpxm_rel_camera;
		litpxm_global_sum += litpxm_rel_camera;
	}

	wrist_global_sum = m_vec3_mul_scalar(wrist_global_sum, 1.0f / (float)num_observation_views);
	midpxm_global_sum = m_vec3_mul_scalar(midpxm_global_sum, 1.0f / (float)num_observation_views);
	indpxm_global_sum = m_vec3_mul_scalar(indpxm_global_sum, 1.0f / (float)num_observation_views);
	litpxm_global_sum = m_vec3_mul_scalar(wrist_global_sum, 1.0f / (float)num_observation_views);

	out_wrist_guess.position = wrist_global_sum;

#if 0
	// Original
	xrt_vec3 plus_z = m_vec3_normalize(wrist_global_sum - midpxm_global_sum);
	xrt_vec3 plus_x = m_vec3_normalize(indpxm_global_sum - litpxm_global_sum);

#elif 0
	// Negated
	xrt_vec3 plus_z = m_vec3_normalize(midpxm_global_sum - wrist_global_sum);
	// Not negated
	xrt_vec3 plus_x = m_vec3_normalize(indpxm_global_sum - litpxm_global_sum);
#elif 0
	// Both negated
	xrt_vec3 plus_z = m_vec3_normalize(midpxm_global_sum - wrist_global_sum);
	xrt_vec3 plus_x = m_vec3_normalize(litpxm_global_sum - indpxm_global_sum);

#else
	// Not negated
	xrt_vec3 plus_z = m_vec3_normalize(wrist_global_sum - midpxm_global_sum);
	// Negated
	xrt_vec3 plus_x = m_vec3_normalize(litpxm_global_sum - indpxm_global_sum);
#endif

	plus_x = m_vec3_orthonormalize(plus_z, plus_x);
	math_quat_from_plus_x_z(&plus_x, &plus_z, &out_wrist_guess.orientation);

	return true;
}
} // namespace xrt::tracking::hand::mercury::lm
