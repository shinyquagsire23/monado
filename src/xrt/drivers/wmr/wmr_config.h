/* Copyright 2021 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 */
/*!
 * @file
 * @brief	WMR and MS HoloLens configuration structures
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_wmr
 */

#pragma once

#include "math/m_vec2.h"
#include "util/u_logging.h"

enum wmr_distortion_model
{
	WMR_DISTORTION_MODEL_UNKNOWN = 0,
	WMR_DISTORTION_MODEL_POLYNOMIAL_3K
};

#ifdef __cplusplus
extern "C" {
#endif

struct wmr_distortion_3K
{
	enum wmr_distortion_model model;

	/* X/Y center of the distortion (pixels) */
	struct xrt_vec2 eye_center;
	/* k1,k2,k3 params for radial distortion as
	 * per the radial distortion model in
	 * https://docs.opencv.org/master/d9/d0c/group__calib3d.html */
	double k[3];
};

struct wmr_distortion_eye_config
{
	/* 3x3 camera matrix that moves from normalised camera coords (X/Z & Y/Z) to undistorted pixels */
	struct xrt_matrix_3x3 affine_xform;
	/* Eye pose in world space */
	struct xrt_pose pose;
	/* Radius of the (undistorted) visible area from the center (pixels) (I think) */
	double visible_radius;

	/* Width, Height (pixels) of the full display */
	struct xrt_vec2 display_size;
	/* Center for the eye viewport visibility (pixels) */
	struct xrt_vec2 visible_center;

	/* RGB distortion params */
	struct wmr_distortion_3K distortion3K[3];
};

struct wmr_hmd_config
{
	/* Left and Right eye mapping and distortion params */
	struct wmr_distortion_eye_config eye_params[2];

	struct xrt_pose accel_pose;
	struct xrt_pose gyro_pose;
	struct xrt_pose mag_pose;
};

bool
wmr_config_parse(struct wmr_hmd_config *c, char *json_string, enum u_logging_level ll);

#ifdef __cplusplus
}
#endif
