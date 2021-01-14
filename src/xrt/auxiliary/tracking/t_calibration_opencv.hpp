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
 * @brief Essential calibration data wrapped for C++.
 *
 * Just like the cv::Mat that it holds, this object does not own all the memory
 * it points to!
 */
struct CameraCalibrationWrapper
{
	t_camera_calibration &base;
	xrt_size &image_size_pixels;
	const cv::Size image_size_pixels_cv;
	cv::Mat_<double> intrinsics_mat;
	cv::Mat_<double> distortion_mat;
	cv::Mat_<double> distortion_fisheye_mat;
	bool &use_fisheye;

	CameraCalibrationWrapper(t_camera_calibration &calib)
	    : base(calib), image_size_pixels(calib.image_size_pixels),
	      image_size_pixels_cv(calib.image_size_pixels.w, calib.image_size_pixels.h),
	      intrinsics_mat(3, 3, &calib.intrinsics[0][0]),
	      distortion_mat(XRT_DISTORTION_MAX_DIM, 1, &calib.distortion[0]),
	      distortion_fisheye_mat(4, 1, &calib.distortion_fisheye[0]), use_fisheye(calib.use_fisheye)
	{
		assert(isDataStorageValid());
	}

	//! Try to verify nothing was reallocated.
	bool
	isDataStorageValid() const noexcept
	{
		return intrinsics_mat.size() == cv::Size(3, 3) &&
		       (double *)intrinsics_mat.data == &(base.intrinsics[0][0]) &&

		       distortion_mat.size() == cv::Size(1, XRT_DISTORTION_MAX_DIM) &&
		       (double *)distortion_mat.data == &(base.distortion[0]) &&

		       distortion_fisheye_mat.size() == cv::Size(1, 4) &&
		       (double *)distortion_fisheye_mat.data == &(base.distortion_fisheye[0]);
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
	t_stereo_camera_calibration *base;
	CameraCalibrationWrapper view[2];
	cv::Mat_<double> camera_translation_mat;
	cv::Mat_<double> camera_rotation_mat;
	cv::Mat_<double> camera_essential_mat;
	cv::Mat_<double> camera_fundamental_mat;


	static t_stereo_camera_calibration *
	allocData()
	{
		t_stereo_camera_calibration *data_ptr = NULL;
		t_stereo_camera_calibration_alloc(&data_ptr);
		return data_ptr;
	}

	StereoCameraCalibrationWrapper(t_stereo_camera_calibration *stereo)
	    : base(stereo), view{CameraCalibrationWrapper{stereo->view[0]}, CameraCalibrationWrapper{stereo->view[1]}},
	      camera_translation_mat(3, 1, &stereo->camera_translation[0]),
	      camera_rotation_mat(3, 3, &stereo->camera_rotation[0][0]),
	      camera_essential_mat(3, 3, &stereo->camera_essential[0][0]),
	      camera_fundamental_mat(3, 3, &stereo->camera_fundamental[0][0])
	{
		// Correct reference counting.
		t_stereo_camera_calibration *temp = NULL;
		t_stereo_camera_calibration_reference(&temp, stereo);

		assert(isDataStorageValid());
	}

	StereoCameraCalibrationWrapper() : StereoCameraCalibrationWrapper(allocData())
	{

		// The function allocData returns with a ref count of one,
		// the constructor increments the refcount with one,
		// so to correct it we need to decrement the ref count with one.
		t_stereo_camera_calibration *tmp = base;
		t_stereo_camera_calibration_reference(&tmp, NULL);
	}

	~StereoCameraCalibrationWrapper()
	{
		t_stereo_camera_calibration_reference(&base, NULL);
	}

	bool
	isDataStorageValid() const noexcept
	{
		return camera_translation_mat.size() == cv::Size(1, 3) &&
		       (double *)camera_translation_mat.data == &base->camera_translation[0] &&

		       camera_rotation_mat.size() == cv::Size(3, 3) &&
		       (double *)camera_rotation_mat.data == &base->camera_rotation[0][0] &&

		       camera_essential_mat.size() == cv::Size(3, 3) &&
		       (double *)camera_essential_mat.data == &base->camera_essential[0][0] &&

		       camera_fundamental_mat.size() == cv::Size(3, 3) &&
		       (double *)camera_fundamental_mat.data == &base->camera_fundamental[0][0] &&

		       view[0].isDataStorageValid() && view[1].isDataStorageValid();
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
calibration_get_undistort_map(t_camera_calibration &calib,
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
	StereoRectificationMaps(t_stereo_camera_calibration *data);
};


/*!
 * @brief Provides cached, precomputed access to normalized image coordinates
 * from original, distorted ones.
 *
 * Populates internal structures using cv::undistortPoints() and performs
 * subpixel sampling to interpolate for each query. Essentially, this class lets
 * you perform cv::undistortPoints() while caching the initial setup work
 * required for that function.
 */
class NormalizedCoordsCache
{
public:
	/*!
	 * @brief Set up the precomputed cache for a given camera.
	 *
	 * @param size Size of the image in pixels
	 * @param intrinsics Camera intrinsics matrix
	 * @param distortion Distortion coefficients
	 *
	 * This overload applies no rectification (`R`) and uses a
	 * normalized/identity new camera matrix (`P`).
	 */
	NormalizedCoordsCache(cv::Size size, const cv::Matx33d &intrinsics, const cv::Matx<double, 5, 1> &distortion);
	/*!
	 * @brief Set up the precomputed cache for a given camera (overload for
	 * rectification and new camera matrix)
	 *
	 * @param size Size of the image in pixels
	 * @param intrinsics Camera intrinsics matrix
	 * @param distortion Distortion coefficients
	 * @param rectification Rectification matrix - corresponds to parameter
	 * `R` to cv::undistortPoints().
	 * @param new_camera_matrix A 3x3 new camera matrix - corresponds to
	 * parameter `P` to cv::undistortPoints().
	 */
	NormalizedCoordsCache(cv::Size size,
	                      const cv::Matx33d &intrinsics,
	                      const cv::Matx<double, 5, 1> &distortion,
	                      const cv::Matx33d &rectification,
	                      const cv::Matx33d &new_camera_matrix);

	/*!
	 * @brief Set up the precomputed cache for a given camera. (overload for
	 * rectification and new projection matrix)
	 *
	 * @param size Size of the image in pixels
	 * @param intrinsics Camera intrinsics matrix
	 * @param distortion Distortion coefficients
	 * @param rectification Rectification matrix - corresponds to parameter
	 * `R` to cv::undistortPoints().
	 * @param new_projection_matrix A 3x4 new projection matrix -
	 * corresponds to parameter `P` to cv::undistortPoints().
	 */
	NormalizedCoordsCache(cv::Size size,
	                      const cv::Matx33d &intrinsics,
	                      const cv::Matx<double, 5, 1> &distortion,
	                      const cv::Matx33d &rectification,
	                      const cv::Matx<double, 3, 4> &new_projection_matrix);

	/*!
	 * @brief Set up the precomputed cache for a given camera.
	 *
	 * Less-strongly-typed overload.
	 *
	 * @overload
	 *
	 * This overload applies no rectification (`R`) and uses a
	 * normalized/identity new camera matrix (`P`).
	 */
	NormalizedCoordsCache(cv::Size size, const cv::Mat &intrinsics, const cv::Mat &distortion);

	/*!
	 * @brief Get normalized, undistorted coordinates from a point in the
	 * original (distorted, etc.) image.
	 *
	 * @param origCoords Image coordinates in original image
	 *
	 * @return Corresponding undistorted coordinates in a "normalized" image
	 */
	cv::Vec2f
	getNormalizedImageCoords(cv::Point2f origCoords) const;

	/*!
	 * @brief Get normalized vector in the camera-space direction
	 * corresponding to the original (distorted, etc.) image coordinates.
	 *
	 * Note that the Z component will be negative by convention.
	 */
	cv::Vec3f
	getNormalizedVector(cv::Point2f origCoords) const;

private:
	cv::Mat_<float> cacheX_;
	cv::Mat_<float> cacheY_;
};
