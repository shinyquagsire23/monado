// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenCV calibration helpers.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
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
 * Save raw calibration data to file, hack until prober has storage for such
 * things.
 */
extern "C" bool
t_file_save_raw_data_hack(struct t_stereo_camera_calibration *data);

/*!
 * @brief Essential calibration data wrapped for C++.
 *
 * Just like the cv::Mat that it holds, this object does not own all the memory
 * it points to!
 */
struct CameraCalibrationWrapper
{
	xrt_size &image_size_pixels;
	cv::Mat_<double> intrinsics_mat;
	cv::Mat_<double> distortion_mat;
	cv::Mat_<double> distortion_fisheye_mat;
	bool &use_fisheye;

	CameraCalibrationWrapper(t_camera_calibration &calib)
	    : image_size_pixels(calib.image_size_pixels),
	      intrinsics_mat(3, 3, &calib.intrinsics[0][0]),
	      distortion_mat(XRT_DISTORTION_MAX_DIM, 1, &calib.distortion[0]),
	      distortion_fisheye_mat(4, 1, &calib.distortion_fisheye[0]),
	      use_fisheye(calib.use_fisheye)
	{
		assert(isDataStorageValid());
	}

	//! Try to verify nothing was reallocated.
	bool
	isDataStorageValid() const noexcept
	{
		return intrinsics_mat.size() == cv::Size(3, 3) &&
		       distortion_mat.size() ==
		           cv::Size(1, XRT_DISTORTION_MAX_DIM) &&
		       distortion_fisheye_mat.size() == cv::Size(1, 4);
	}
};


/*!
 * @brief Essential stereo calibration data wrapped for C++.
 *
 * Just like the cv::Mat that it holds, this object does not own (all) the
 * memory it points to!
 */
struct StereoCameraCalibrationWrapper
{
	CameraCalibrationWrapper view[2];
	cv::Mat_<double> camera_translation_mat;
	cv::Mat_<double> camera_rotation_mat;
	cv::Mat_<double> camera_essential_mat;
	cv::Mat_<double> camera_fundamental_mat;

	StereoCameraCalibrationWrapper(t_stereo_camera_calibration &stereo)
	    : view{CameraCalibrationWrapper{stereo.view[0]},
	           CameraCalibrationWrapper{stereo.view[1]}},
	      camera_translation_mat(3, 1, &stereo.camera_translation[0]),
	      camera_rotation_mat(3, 3, &stereo.camera_rotation[0][0]),
	      camera_essential_mat(3, 3, &stereo.camera_essential[0][0]),
	      camera_fundamental_mat(3, 3, &stereo.camera_fundamental[0][0])
	{
		assert(isDataStorageValid());
	}

	bool
	isDataStorageValid() const noexcept
	{
		return camera_translation_mat.size() == cv::Size(1, 3) &&
		       camera_rotation_mat.size() == cv::Size(3, 3) &&

		       camera_essential_mat.size() == cv::Size(3, 3) &&
		       camera_fundamental_mat.size() == cv::Size(3, 3) &&
		       view[0].isDataStorageValid() &&
		       view[1].isDataStorageValid();
	}
};


/*!
 * @brief An x,y pair of matrices for the remap() function.
 *
 * @see calibration_get_undistort_map
 */
struct RemapPair
{
	cv::Mat remap_x;
	cv::Mat remap_y;
};

/*!
 * @brief Prepare undistortion/normalization remap structures for a rectilinear
 * or fisheye image.
 *
 * @param calib A single camera calibration structure.
 * @param rectify_transform_optional A rectification transform to apply, if
 * desired.
 * @param new_camera_matrix_optional Unlike OpenCV, the default/empty matrix
 * here uses the input camera matrix as your output camera matrix.
 */
RemapPair
calibration_get_undistort_map(
    t_camera_calibration &calib,
    cv::InputArray rectify_transform_optional = cv::noArray(),
    cv::Mat new_camera_matrix_optional = cv::Mat());

/*!
 * @brief Rectification, rotation, projection data for a single view in a stereo
 * pair.
 *
 * @see StereoRectificationMaps
 */
struct ViewRectification
{
	RemapPair rectify;
	cv::Mat rotation_mat = {};
	cv::Mat projection_mat = {};
};

/*!
 * @brief Rectification maps as well as transforms for a stereo camera.
 *
 * Computed in the constructor from saved calibration data.
 */
struct StereoRectificationMaps
{
	ViewRectification view[2];

	//! Disparity and position to camera world coordinates.
	cv::Mat disparity_to_depth_mat = {};

	/*!
	 * @brief Constructor - produces rectification data for a stereo camera
	 * based on calibration data.
	 */
	StereoRectificationMaps(t_stereo_camera_calibration &data);
};
