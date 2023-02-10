/*
 * Copyright 2022 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 */
/*!
 * @file
 * @brief  Driver code for Oculus Rift S headsets
 *
 * Utility functions for generating a stereo camera calibration,
 * and converting the Rift S Fisheye62 distortion parameters into
 * OpenCV-compatible Kannala-Brandt parameters
 *
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */
#include <stdbool.h>
#include <math.h>

#include "tinyceres/tiny_solver.hpp"
#include "tinyceres/tiny_solver_autodiff_function.hpp"

#include "rift_s_util.h"

using ceres::TinySolver;
using ceres::TinySolverAutoDiffFunction;

const int N_KB4_DISTORT_PARAMS = 4;

template <typename T>
bool
fisheye62_undistort_func(struct t_camera_calibration *calib,
                         const double *distortion_params,
                         const T point[2],
                         T *out_point)
{
	const T x = point[0];
	const T y = point[1];

	const T r2 = x * x + y * y;
	const T r = sqrt(r2);

	const double fx = calib->intrinsics[0][0];
	const double fy = calib->intrinsics[1][1];

	const double cx = calib->intrinsics[0][2];
	const double cy = calib->intrinsics[1][2];

	if (r < 1e-8) {
		out_point[0] = fx * x + cx;
		out_point[1] = fy * y + cy;
		return true;
	}

	const T theta = atan(r);
	const T theta2 = theta * theta;

	const T xp = x * theta / r;
	const T yp = y * theta / r;

	const double k1 = distortion_params[0];
	const double k2 = distortion_params[1];
	const double k3 = distortion_params[2];
	const double k4 = distortion_params[3];
	const double k5 = distortion_params[4];
	const double k6 = distortion_params[5];
	const double p1 = distortion_params[6];
	const double p2 = distortion_params[7];

	/* 1 + k1 * theta^2 + k2 * theta^4 + k3 * theta^6 + k4 * theta^8 + k5 * theta^10 + k6 * theta^12 */
	T r_theta = k6 * theta2;
	r_theta += k5;
	r_theta *= theta2;
	r_theta += k4;
	r_theta *= theta2;
	r_theta += k3;
	r_theta *= theta2;
	r_theta += k2;
	r_theta *= theta2;
	r_theta += k1;
	r_theta *= theta2;
	r_theta += 1;

	T delta_x = 2 * p1 * xp * yp + p2 * (theta2 + xp * xp * 2.0);
	T delta_y = 2 * p2 * xp * yp + p1 * (theta2 + yp * yp * 2.0);

	const T mx = xp * r_theta + delta_x;
	const T my = yp * r_theta + delta_y;

	out_point[0] = fx * mx + cx;
	out_point[1] = fy * my + cy;

	return true;
}

struct UndistortCostFunctor
{
	UndistortCostFunctor(struct t_camera_calibration *calib, double *distortion_params, double point[2])
	    : m_calib(calib), m_distortion_params(distortion_params)
	{
		m_point[0] = point[0];
		m_point[1] = point[1];
	}

	struct t_camera_calibration *m_calib;
	double *m_distortion_params;
	double m_point[2];

	template <typename T>
	bool
	operator()(const T *const x, T *residual) const
	{
		T out_point[2];

		if (!fisheye62_undistort_func(m_calib, m_distortion_params, x, out_point))
			return false;

		residual[0] = out_point[0] - m_point[0];
		residual[1] = out_point[1] - m_point[1];
		return true;
	}
};

template <typename T>
bool
kb4_distort_func(struct t_camera_calibration *calib, const T *distortion_params, const double point[2], T *out_point)
{
	const double x = point[0];
	const double y = point[1];

	const double r2 = x * x + y * y;
	const double r = sqrt(r2);

	const double fx = calib->intrinsics[0][0];
	const double fy = calib->intrinsics[1][1];

	const double cx = calib->intrinsics[0][2];
	const double cy = calib->intrinsics[1][2];

	if (r < 1e-8) {
		out_point[0] = T(fx * x + cx);
		out_point[1] = T(fy * y + cy);
		return true;
	}

	const double theta = atan(r);
	const double theta2 = theta * theta;

	const T k1 = distortion_params[0];
	const T k2 = distortion_params[1];
	const T k3 = distortion_params[2];
	const T k4 = distortion_params[3];

	T r_theta = k4 * theta2;

	r_theta += k3;
	r_theta *= theta2;
	r_theta += k2;
	r_theta *= theta2;
	r_theta += k1;
	r_theta *= theta2;
	r_theta += 1;
	r_theta *= theta;

	const T mx = x * r_theta / r;
	const T my = y * r_theta / r;

	out_point[0] = fx * mx + cx;
	out_point[1] = fy * my + cy;

	return true;
}

struct TargetPoint
{
	double point[2];
	double distorted[2];
};

struct DistortParamKB4CostFunctor
{
	DistortParamKB4CostFunctor(struct t_camera_calibration *calib, int nSteps, TargetPoint *targetPointGrid)
	    : m_calib(calib), m_nSteps(nSteps), m_targetPointGrid(targetPointGrid)
	{}

	struct t_camera_calibration *m_calib;
	int m_nSteps;
	TargetPoint *m_targetPointGrid;

	template <typename T>
	bool
	operator()(const T *const distort_params, T *residual) const
	{
		T out_point[2];

		for (int y_index = 0; y_index < m_nSteps; y_index++) {
			for (int x_index = 0; x_index < m_nSteps; x_index++) {
				int residual_index = 2 * (y_index * m_nSteps + x_index);
				TargetPoint *p = &m_targetPointGrid[(y_index * m_nSteps) + x_index];

				if (!kb4_distort_func<T>(m_calib, distort_params, p->point, out_point))
					return false;

				residual[residual_index + 0] = out_point[0] - p->distorted[0];
				residual[residual_index + 1] = out_point[1] - p->distorted[1];
			}
		}

		return true;
	}
};

#define STEPS 21
struct t_camera_calibration
rift_s_get_cam_calib(struct rift_s_camera_calibration_block *camera_calibration, enum rift_s_camera_id cam_id)
{
	struct t_camera_calibration tcc;

	struct rift_s_camera_calibration *rift_s_cam = &camera_calibration->cameras[cam_id];
	tcc.image_size_pixels.h = rift_s_cam->roi.extent.h;
	tcc.image_size_pixels.w = rift_s_cam->roi.extent.w;
	tcc.intrinsics[0][0] = rift_s_cam->projection.fx;
	tcc.intrinsics[1][1] = rift_s_cam->projection.fy;
	tcc.intrinsics[0][2] = rift_s_cam->projection.cx;
	tcc.intrinsics[1][2] = rift_s_cam->projection.cy;
	tcc.intrinsics[2][2] = 1.0;
	tcc.distortion_model = T_DISTORTION_FISHEYE_KB4;

	TargetPoint xy[STEPS * STEPS];

	/* Convert fisheye62 params to KB4: */
	double fisheye62_distort_params[8];
	for (int i = 0; i < 6; i++) {
		fisheye62_distort_params[i] = rift_s_cam->distortion.k[i];
	}
	fisheye62_distort_params[6] = rift_s_cam->distortion.p1;
	fisheye62_distort_params[7] = rift_s_cam->distortion.p2;

	/* Calculate Fisheye62 distortion grid by finding the viewplane coordinates that
	 * project onto the points of grid spaced across the pixel image plane */
	for (int y_index = 0; y_index < STEPS; y_index++) {
		for (int x_index = 0; x_index < STEPS; x_index++) {
			int x = x_index * (tcc.image_size_pixels.w - 1) / (STEPS - 1);
			int y = y_index * (tcc.image_size_pixels.h - 1) / (STEPS - 1);
			TargetPoint *p = &xy[(y_index * STEPS) + x_index];

			p->distorted[0] = x;
			p->distorted[1] = y;

			Eigen::Matrix<double, 2, 1> result(0, 0);

			using AutoDiffUndistortFunction = TinySolverAutoDiffFunction<UndistortCostFunctor, 2, 2>;
			UndistortCostFunctor undistort_functor(&tcc, fisheye62_distort_params, p->distorted);
			AutoDiffUndistortFunction f(undistort_functor);

			TinySolver<AutoDiffUndistortFunction> solver;
			solver.Solve(f, &result);

			p->point[0] = result[0];
			p->point[1] = result[1];
		}
	}

	/* Use the calculated distortion grid to solve for kb4 params */
	{
		Eigen::Matrix<double, N_KB4_DISTORT_PARAMS, 1> kb4_distort_params;

		using AutoDiffDistortParamKB4Function =
		    TinySolverAutoDiffFunction<DistortParamKB4CostFunctor, 2 * STEPS * STEPS, N_KB4_DISTORT_PARAMS>;
		DistortParamKB4CostFunctor distort_param_kb4_functor(&tcc, STEPS, xy);
		AutoDiffDistortParamKB4Function f(distort_param_kb4_functor);

		TinySolver<AutoDiffDistortParamKB4Function> solver;
		solver.Solve(f, &kb4_distort_params);

		tcc.kb4.k1 = kb4_distort_params[0];
		tcc.kb4.k2 = kb4_distort_params[1];
		tcc.kb4.k3 = kb4_distort_params[2];
		tcc.kb4.k4 = kb4_distort_params[3];

		return tcc;
	}
}

/*!
 * Allocate and populate an OpenCV-compatible @ref t_stereo_camera_calibration pointer from
 * the Rift S config.
 *
 * This requires fitting a KB4 fisheye polynomial to the 6 radial + 2 tangential 'Fisheye62'
 * parameters provided by the Rift S.
 *
 */
struct t_stereo_camera_calibration *
rift_s_create_stereo_camera_calib_rotated(struct rift_s_camera_calibration_block *camera_calibration)
{
	struct t_stereo_camera_calibration *calib = NULL;
	t_stereo_camera_calibration_alloc(&calib, T_DISTORTION_FISHEYE_KB4);

	struct rift_s_camera_calibration *left = &camera_calibration->cameras[RIFT_S_CAMERA_FRONT_LEFT];
	struct rift_s_camera_calibration *right = &camera_calibration->cameras[RIFT_S_CAMERA_FRONT_RIGHT];

	// Intrinsics
	for (int view = 0; view < 2; view++) {
		enum rift_s_camera_id cam_id = view == 0 ? RIFT_S_CAMERA_FRONT_LEFT : RIFT_S_CAMERA_FRONT_RIGHT;
		calib->view[view] = rift_s_get_cam_calib(camera_calibration, cam_id);
	}

	struct xrt_pose device_from_left, device_from_right;
	struct xrt_pose right_from_device, right_from_left;

	struct xrt_matrix_3x3 right_from_left_rot;

	/* Compute the transform from the left eye to the right eye
	 * by using the config provided camera->device transform
	 */
	math_pose_from_isometry(&left->device_from_camera, &device_from_left);
	math_pose_from_isometry(&right->device_from_camera, &device_from_right);

	math_pose_invert(&device_from_right, &right_from_device);
	math_pose_transform(&device_from_left, &right_from_device, &right_from_left);

	math_matrix_3x3_from_quat(&right_from_left.orientation, &right_from_left_rot);

	/* Rotate the position in the camera extrinsics 90° to
	 * compensate for the front cams being rotated. That means hand poses
	 * are detected and come out rotated too, so need correcting
	 * in the tracking override offset */
	calib->camera_translation[0] = -right_from_left.position.y;
	calib->camera_translation[1] = right_from_left.position.x;
	calib->camera_translation[2] = right_from_left.position.z;

	calib->camera_rotation[0][0] = right_from_left_rot.v[0];
	calib->camera_rotation[0][1] = right_from_left_rot.v[1];
	calib->camera_rotation[0][2] = right_from_left_rot.v[2];
	calib->camera_rotation[1][0] = right_from_left_rot.v[3];
	calib->camera_rotation[1][1] = right_from_left_rot.v[4];
	calib->camera_rotation[1][2] = right_from_left_rot.v[5];
	calib->camera_rotation[2][0] = right_from_left_rot.v[6];
	calib->camera_rotation[2][1] = right_from_left_rot.v[7];
	calib->camera_rotation[2][2] = right_from_left_rot.v[8];

	return calib;
}
