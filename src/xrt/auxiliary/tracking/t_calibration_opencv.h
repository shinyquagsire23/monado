// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenCV calibration helpers.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#pragma once

#ifndef __cplusplus
#error "This header is C++-only."
#endif

#include "tracking/t_tracking.h"

#include <opencv2/opencv.hpp>
#include <sys/stat.h>


/*!
 * Save raw settings to file, hack until prober have a settings file.
 */
extern "C" bool
t_file_save_raw_data_hack(struct t_settings_stereo_raw *raw_data);

/*!
 * Refined calibration data.
 */
struct CalibrationData : t_settings_stereo
{
public:
	cv::Mat disparity_to_depth = {};

	cv::Mat l_undistort_map_x = {};
	cv::Mat l_undistort_map_y = {};
	cv::Mat l_rectify_map_x = {};
	cv::Mat l_rectify_map_y = {};

	cv::Mat r_undistort_map_x = {};
	cv::Mat r_undistort_map_y = {};
	cv::Mat r_rectify_map_x = {};
	cv::Mat r_rectify_map_y = {};
};

/*!
 * What calibration data that is saved down to file.
 */
struct CalibrationRawData : t_settings_stereo_raw
{
public:
	cv::Mat camera_rotation_mat = {};
	cv::Mat camera_translation_mat = {};
	cv::Mat camera_essential_mat = {};
	cv::Mat camera_fundamental_mat = {};

	cv::Mat disparity_to_depth_mat = {};

	cv::Mat l_intrinsics_mat = {};
	cv::Mat l_distortion_mat = {};
	cv::Mat l_distortion_fisheye_mat = {};
	cv::Mat l_rotation_mat = {};
	cv::Mat l_projection_mat = {};

	cv::Mat r_intrinsics_mat = {};
	cv::Mat r_distortion_mat = {};
	cv::Mat r_distortion_fisheye_mat = {};
	cv::Mat r_rotation_mat = {};
	cv::Mat r_projection_mat = {};


public:
	CalibrationRawData()
	{
		// clang-format off
		camera_translation_mat = cv::Mat(3, 1, CV_64F, &camera_translation[0]);
		camera_rotation_mat = cv::Mat(3, 3, CV_64F, &camera_rotation[0][0]);
		camera_essential_mat = cv::Mat(3, 3, CV_64F, &camera_essential[0][0]);
		camera_fundamental_mat = cv::Mat(3, 3, CV_64F, &camera_fundamental[0][0]);

		l_intrinsics_mat = cv::Mat(3, 3, CV_64F, &l_intrinsics[0][0]);
		l_distortion_mat = cv::Mat(1, 5, CV_64F, &l_distortion[0]);
		l_distortion_fisheye_mat = cv::Mat(1, 4, CV_64F, &l_distortion_fisheye[0]);
		l_rotation_mat = cv::Mat(3, 3, CV_64F, &l_rotation[0][0]);
		l_projection_mat = cv::Mat(3, 4, CV_64F, &l_projection[0][0]);

		r_intrinsics_mat = cv::Mat(3, 3, CV_64F, &r_intrinsics[0][0]);
		r_distortion_mat = cv::Mat(1, 5, CV_64F, &r_distortion[0]);
		r_distortion_fisheye_mat = cv::Mat(1, 4, CV_64F, &r_distortion_fisheye[0]);
		r_rotation_mat = cv::Mat(3, 3, CV_64F, &r_rotation[0][0]);
		r_projection_mat = cv::Mat(3, 4, CV_64F, &r_projection[0][0]);
		// clang-format on
	}

	bool
	isDataStorageValid()
	{
		return camera_rotation_mat.size() == cv::Size(3, 3) &&
		       camera_translation_mat.size() == cv::Size(1, 3) &&
		       camera_essential_mat.size() == cv::Size(3, 3) &&
		       camera_fundamental_mat.size() == cv::Size(3, 3) &&
		       l_intrinsics_mat.size() == cv::Size(3, 3) &&
		       l_distortion_mat.size() == cv::Size(5, 1) &&
		       l_distortion_fisheye_mat.size() == cv::Size(4, 1) &&
		       l_rotation_mat.size() == cv::Size(3, 3) &&
		       l_projection_mat.size() == cv::Size(4, 3) &&
		       r_intrinsics_mat.size() == cv::Size(3, 3) &&
		       r_distortion_mat.size() == cv::Size(5, 1) &&
		       r_distortion_fisheye_mat.size() == cv::Size(4, 1) &&
		       r_rotation_mat.size() == cv::Size(3, 3) &&
		       r_projection_mat.size() == cv::Size(4, 3);
	}
};

XRT_MAYBE_UNUSED static void
calibration_get_stereo(t_settings_stereo *data_c,
                       cv::Mat *l_undistort_map_x,
                       cv::Mat *l_undistort_map_y,
                       cv::Mat *l_rectify_map_x,
                       cv::Mat *l_rectify_map_y,
                       cv::Mat *r_undistort_map_x,
                       cv::Mat *r_undistort_map_y,
                       cv::Mat *r_rectify_map_x,
                       cv::Mat *r_rectify_map_y,
                       cv::Mat *disparity_to_depth)
{
	CalibrationData *data = (CalibrationData *)data_c;

	*l_undistort_map_x = data->l_undistort_map_x;
	*l_undistort_map_y = data->l_undistort_map_y;
	*l_rectify_map_x = data->l_rectify_map_x;
	*l_rectify_map_y = data->l_rectify_map_y;
	*r_undistort_map_x = data->r_undistort_map_x;
	*r_undistort_map_y = data->r_undistort_map_y;
	*r_rectify_map_x = data->r_rectify_map_x;
	*r_rectify_map_y = data->r_rectify_map_y;
	*disparity_to_depth = data->disparity_to_depth;
}
