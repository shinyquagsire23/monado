// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenCV calibration helpers.
 * @author Pete Black <pblack@collabora.com>
 * @ingroup aux_tracking
 */

#pragma once

#include "tracking/t_tracking.h"

#include <opencv2/opencv.hpp>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * What calibration data that is saved down to file.
 */
struct CalibrationRawData : t_calibration_raw_data
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
	cv::Mat l_translation_mat = {};
	cv::Mat l_rotation_mat = {};
	cv::Mat l_projection_mat = {};
	cv::Mat r_intrinsics_mat = {};
	cv::Mat r_distortion_mat = {};
	cv::Mat r_distortion_fisheye_mat = {};
	cv::Mat r_translation_mat = {};
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
		l_rotation_mat = cv::Mat(3, 3, CV_64F, &l_rotation[0][0]);
		l_projection_mat = cv::Mat(3, 4, CV_64F, &l_projection[0][0]);

		r_intrinsics_mat = cv::Mat(3, 3, CV_64F, &r_intrinsics[0][0]);
		r_distortion_mat = cv::Mat(1, 5, CV_64F, &r_distortion[0]);
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
		       l_distortion_fisheye_mat.size() == cv::Size(0, 0) &&
		       l_translation_mat.size() == cv::Size(0, 0) &&
		       l_rotation_mat.size() == cv::Size(3, 3) &&
		       l_projection_mat.size() == cv::Size(4, 3) &&
		       r_intrinsics_mat.size() == cv::Size(3, 3) &&
		       r_distortion_mat.size() == cv::Size(5, 1) &&
		       r_distortion_fisheye_mat.size() == cv::Size(0, 0) &&
		       r_translation_mat.size() == cv::Size(0, 0) &&
		       r_rotation_mat.size() == cv::Size(3, 3) &&
		       r_projection_mat.size() == cv::Size(4, 3);
	}
};

struct CalibrationData : t_calibration_data
{
	cv::Mat l_undistort_map_x = {};
	cv::Mat l_undistort_map_y = {};
	cv::Mat l_rectify_map_x = {};
	cv::Mat l_rectify_map_y = {};
	cv::Mat r_undistort_map_x = {};
	cv::Mat r_undistort_map_y = {};
	cv::Mat r_rectify_map_x = {};
	cv::Mat r_rectify_map_y = {};
	cv::Mat disparity_to_depth = {};
};

extern "C" bool
t_file_load_stereo_calibration_v1_hack(struct t_calibration_data **out_data);

extern "C" bool
t_file_save_raw_data_hack(struct t_calibration_raw_data *raw_data);

XRT_MAYBE_UNUSED static bool
calibration_get_stereo(const char *configuration_filename,
                       uint32_t frame_w,
                       uint32_t frame_h,
                       bool use_fisheye,
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
	t_calibration_data *data_c;
	bool ok = t_file_load_stereo_calibration_v1_hack(&data_c);

	if (!ok) {
		return false;
	}

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

	t_calibration_data_free(data_c);

	return true;
}

//! @todo Move this as it is a generic helper
XRT_MAYBE_UNUSED static int
mkpath(char *path)
{
	char tmp[PATH_MAX]; //!< @todo PATH_MAX probably not strictly correct
	char *p = nullptr;
	size_t len;

	snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp) - 1;
	if (tmp[len] == '/') {
		tmp[len] = 0;
	}

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (mkdir(tmp, S_IRWXU) < 0 && errno != EEXIST)
				return -1;
			*p = '/';
		}
	}

	if (mkdir(tmp, S_IRWXU) < 0 && errno != EEXIST) {
		return -1;
	}

	return 0;
}


#ifdef __cplusplus
}
#endif
