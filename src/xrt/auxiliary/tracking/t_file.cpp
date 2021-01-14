// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Handling of files and calibration data.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_tracking
 */

#include "tracking/t_calibration_opencv.hpp"
#include "util/u_misc.h"
#include "util/u_logging.h"

/*
 *
 * Pre-declar functions.
 *
 */

static bool
read_cv_mat(FILE *f, cv::Mat *m, const char *name);

static bool
write_cv_mat(FILE *f, cv::Mat *m);


/*
 *
 * Refine and create functions.
 *
 */

RemapPair
calibration_get_undistort_map(t_camera_calibration &calib,
                              cv::InputArray rectify_transform_optional,
                              cv::Mat new_camera_matrix_optional)
{
	RemapPair ret;
	CameraCalibrationWrapper wrap(calib);
	if (new_camera_matrix_optional.empty()) {
		new_camera_matrix_optional = wrap.intrinsics_mat;
	}

	//! @todo Scale Our intrinsics if the frame size we request
	//              calibration for does not match what was saved
	cv::Size image_size(calib.image_size_pixels.w, calib.image_size_pixels.h);

	if (calib.use_fisheye) {
		cv::fisheye::initUndistortRectifyMap(wrap.intrinsics_mat,         // cameraMatrix
		                                     wrap.distortion_fisheye_mat, // distCoeffs
		                                     rectify_transform_optional,  // R
		                                     new_camera_matrix_optional,  // newCameraMatrix
		                                     image_size,                  // size
		                                     CV_32FC1,                    // m1type
		                                     ret.remap_x,                 // map1
		                                     ret.remap_y);                // map2
	} else {
		cv::initUndistortRectifyMap(wrap.intrinsics_mat,        // cameraMatrix
		                            wrap.distortion_mat,        // distCoeffs
		                            rectify_transform_optional, // R
		                            new_camera_matrix_optional, // newCameraMatrix
		                            image_size,                 // size
		                            CV_32FC1,                   // m1type
		                            ret.remap_x,                // map1
		                            ret.remap_y);               // map2
	}

	return ret;
}

StereoRectificationMaps::StereoRectificationMaps(t_stereo_camera_calibration *data)
{
	assert(data != NULL);
	assert(data->view[0].image_size_pixels.w == data->view[1].image_size_pixels.w);
	assert(data->view[0].image_size_pixels.h == data->view[1].image_size_pixels.h);

	assert(data->view[0].use_fisheye == data->view[1].use_fisheye);

	cv::Size image_size(data->view[0].image_size_pixels.w, data->view[0].image_size_pixels.h);
	StereoCameraCalibrationWrapper wrapped(data);

	/*
	 * Generate our rectification maps
	 *
	 * Here cv::noArray() means zero distortion.
	 */
	if (data->view[0].use_fisheye) {
#if 0
		//! @todo for some reason this looks weird?
		// Alpha of 1.0 kinda works, not really.
		int flags = cv::CALIB_ZERO_DISPARITY;
		double balance = 0.0; // aka alpha.
		double fov_scale = 1.0;

		cv::fisheye::stereoRectify(
		    wrapped.view[0].intrinsics_mat,         // K1
		    wrapped.view[0].distortion_fisheye_mat, // D1
		    wrapped.view[1].intrinsics_mat,         // K2
		    wrapped.view[1].distortion_fisheye_mat, // D2
		    image_size,                             // imageSize
		    wrapped.camera_rotation_mat,            // R
		    wrapped.camera_translation_mat,         // tvec
		    view[0].rotation_mat,                   // R1
		    view[1].rotation_mat,                   // R2
		    view[0].projection_mat,                 // P1
		    view[1].projection_mat,                 // P2
		    disparity_to_depth_mat,                 // Q
		    flags,                                  // flags
		    cv::Size(),                             // newImageSize
		    balance,                                // balance
		    fov_scale);                             // fov_scale
#else
		// Regular stereoRectify function instead, without distortion.
		int flags = cv::CALIB_ZERO_DISPARITY;
		// The function performs the default scaling.
		float alpha = -1.0f;

		cv::stereoRectify(wrapped.view[0].intrinsics_mat, // cameraMatrix1
		                  cv::noArray(),                  // distCoeffs1
		                  wrapped.view[1].intrinsics_mat, // cameraMatrix2
		                  cv::noArray(),                  // distCoeffs2
		                  image_size,                     // imageSize
		                  wrapped.camera_rotation_mat,    // R
		                  wrapped.camera_translation_mat, // T
		                  view[0].rotation_mat,           // R1
		                  view[1].rotation_mat,           // R2
		                  view[0].projection_mat,         // P1
		                  view[1].projection_mat,         // P2
		                  disparity_to_depth_mat,         // Q
		                  flags,                          // flags
		                  alpha,                          // alpha
		                  cv::Size(),                     // newImageSize
		                  NULL,                           // validPixROI1
		                  NULL);                          // validPixROI2
#endif
	} else {
		// Have the same principal point on both.
		int flags = cv::CALIB_ZERO_DISPARITY;
		// Get all of the pixels from the camera.
		float alpha = 1.0f;

		cv::stereoRectify(wrapped.view[0].intrinsics_mat, // cameraMatrix1
		                  /* cv::noArray(), */            // distCoeffs1
		                  wrapped.view[0].distortion_mat, // distCoeffs1
		                  wrapped.view[1].intrinsics_mat, // cameraMatrix2
		                  /* cv::noArray(), */            // distCoeffs2
		                  wrapped.view[1].distortion_mat, // distCoeffs2
		                  image_size,                     // imageSize
		                  wrapped.camera_rotation_mat,    // R
		                  wrapped.camera_translation_mat, // T
		                  view[0].rotation_mat,           // R1
		                  view[1].rotation_mat,           // R2
		                  view[0].projection_mat,         // P1
		                  view[1].projection_mat,         // P2
		                  disparity_to_depth_mat,         // Q
		                  flags,                          // flags
		                  alpha,                          // alpha
		                  cv::Size(),                     // newImageSize
		                  NULL,                           // validPixROI1
		                  NULL);                          // validPixROI2
	}

	view[0].rectify = calibration_get_undistort_map(data->view[0], view[0].rotation_mat, view[0].projection_mat);
	view[1].rectify = calibration_get_undistort_map(data->view[1], view[1].rotation_mat, view[1].projection_mat);
}

/*
 *
 * Load functions.
 *
 */

extern "C" bool
t_stereo_camera_calibration_load_v1(FILE *calib_file, struct t_stereo_camera_calibration **out_data)
{
	t_stereo_camera_calibration *data_ptr = NULL;
	t_stereo_camera_calibration_alloc(&data_ptr);
	StereoCameraCalibrationWrapper wrapped(data_ptr);

	// Dummy matrix
	cv::Mat dummy;

	// Read our calibration from this file
	// clang-format off
	cv::Mat_<float> mat_image_size(2, 1);
	bool result = read_cv_mat(calib_file, &wrapped.view[0].intrinsics_mat, "l_intrinsics"); // 3 x 3
	result = result && read_cv_mat(calib_file, &wrapped.view[1].intrinsics_mat, "r_intrinsics"); // 3 x 3
	result = result && read_cv_mat(calib_file, &wrapped.view[0].distortion_mat, "l_distortion"); // 1 x 5
	result = result && read_cv_mat(calib_file, &wrapped.view[1].distortion_mat, "r_distortion"); // 1 x 5
	result = result && read_cv_mat(calib_file, &wrapped.view[0].distortion_fisheye_mat, "l_distortion_fisheye"); // 4 x 1
	result = result && read_cv_mat(calib_file, &wrapped.view[1].distortion_fisheye_mat, "r_distortion_fisheye"); // 4 x 1
	result = result && read_cv_mat(calib_file, &dummy, "l_rotation"); // 3 x 3
	result = result && read_cv_mat(calib_file, &dummy, "r_rotation"); // 3 x 3
	result = result && read_cv_mat(calib_file, &dummy, "l_translation"); // empty
	result = result && read_cv_mat(calib_file, &dummy, "r_translation"); // empty
	result = result && read_cv_mat(calib_file, &dummy, "l_projection"); // 3 x 4
	result = result && read_cv_mat(calib_file, &dummy, "r_projection"); // 3 x 4
	result = result && read_cv_mat(calib_file, &dummy, "disparity_to_depth");  // 4 x 4
	result = result && read_cv_mat(calib_file, &mat_image_size, "mat_image_size");

	if (!result) {
		U_LOG_W("Re-run calibration!");
		return false;
	}
	wrapped.view[0].image_size_pixels.w = uint32_t(mat_image_size(0, 0));
	wrapped.view[0].image_size_pixels.h = uint32_t(mat_image_size(0, 1));
	wrapped.view[1].image_size_pixels = wrapped.view[0].image_size_pixels;

	cv::Mat mat_new_image_size = mat_image_size.clone();
	if (read_cv_mat(calib_file, &mat_new_image_size, "mat_new_image_size")) {
		// do nothing particular here.
	}

	if (!read_cv_mat(calib_file, &wrapped.camera_translation_mat, "translation")) {
		U_LOG_W("Re-run calibration!");
	}
	if (!read_cv_mat(calib_file, &wrapped.camera_rotation_mat, "rotation")) {
		U_LOG_W("Re-run calibration!");
	}
	if (!read_cv_mat(calib_file, &wrapped.camera_essential_mat, "essential")) {
		U_LOG_W("Re-run calibration!");
	}
	if (!read_cv_mat(calib_file, &wrapped.camera_fundamental_mat, "fundamental")) {
		U_LOG_W("Re-run calibration!");
	}

	cv::Mat_<float> mat_use_fisheye(1, 1);
	if (!read_cv_mat(calib_file, &mat_use_fisheye, "use_fisheye")) {
		wrapped.view[0].use_fisheye = false;
		U_LOG_W("Re-run calibration! (Assuming not fisheye)");
	} else {
		wrapped.view[0].use_fisheye = mat_use_fisheye(0, 0) != 0.0f;
	}
	wrapped.view[1].use_fisheye = wrapped.view[0].use_fisheye;
	// clang-format on


	assert(wrapped.isDataStorageValid());

	t_stereo_camera_calibration_reference(out_data, data_ptr);
	t_stereo_camera_calibration_reference(&data_ptr, NULL);

	return true;
}


/*
 *
 * Save functions.
 *
 */

extern "C" bool
t_stereo_camera_calibration_save_v1(FILE *calib_file, struct t_stereo_camera_calibration *data)
{
	StereoCameraCalibrationWrapper wrapped(data);
	// Dummy matrix
	cv::Mat dummy;


	write_cv_mat(calib_file, &wrapped.view[0].intrinsics_mat);
	write_cv_mat(calib_file, &wrapped.view[1].intrinsics_mat);
	write_cv_mat(calib_file, &wrapped.view[0].distortion_mat);
	write_cv_mat(calib_file, &wrapped.view[1].distortion_mat);
	write_cv_mat(calib_file, &wrapped.view[0].distortion_fisheye_mat);
	write_cv_mat(calib_file, &wrapped.view[1].distortion_fisheye_mat);
	write_cv_mat(calib_file, &dummy); // view[0].rotation_mat
	write_cv_mat(calib_file, &dummy); // view[1].rotation_mat
	write_cv_mat(calib_file, &dummy); // l_translation
	write_cv_mat(calib_file, &dummy); // r_translation
	write_cv_mat(calib_file, &dummy); // view[0].projection_mat
	write_cv_mat(calib_file, &dummy); // view[1].projection_mat
	write_cv_mat(calib_file, &dummy); // disparity_to_depth_mat

	cv::Mat mat_image_size;
	mat_image_size.create(1, 2, CV_32F);
	mat_image_size.at<float>(0, 0) = wrapped.view[0].image_size_pixels.w;
	mat_image_size.at<float>(0, 1) = wrapped.view[0].image_size_pixels.h;
	write_cv_mat(calib_file, &mat_image_size);

	// "new" image size - we actually leave up to the caller now
	write_cv_mat(calib_file, &mat_image_size);

	write_cv_mat(calib_file, &wrapped.camera_translation_mat);
	write_cv_mat(calib_file, &wrapped.camera_rotation_mat);
	write_cv_mat(calib_file, &wrapped.camera_essential_mat);
	write_cv_mat(calib_file, &wrapped.camera_fundamental_mat);

	cv::Mat mat_use_fisheye;
	mat_use_fisheye.create(1, 1, CV_32F);
	mat_use_fisheye.at<float>(0, 0) = wrapped.view[0].use_fisheye;
	write_cv_mat(calib_file, &mat_use_fisheye);

	return true;
}


/*
 *
 * Helpers
 *
 */

static bool
write_cv_mat(FILE *f, cv::Mat *m)
{
	uint32_t header[3];
	header[0] = static_cast<uint32_t>(m->elemSize());
	header[1] = static_cast<uint32_t>(m->rows);
	header[2] = static_cast<uint32_t>(m->cols);
	fwrite(static_cast<void *>(header), sizeof(uint32_t), 3, f);
	fwrite(static_cast<void *>(m->data), header[0], header[1] * header[2], f);
	return true;
}

static bool
read_cv_mat(FILE *f, cv::Mat *m, const char *name)
{
	uint32_t header[3] = {};
	size_t read = 0;

	cv::Mat temp;
	read = fread(static_cast<void *>(header), sizeof(uint32_t), 3, f);
	if (read != 3) {
		U_LOG_E("Failed to read mat header: '%i' '%s'", (int)read, name);
		return false;
	}

	if (header[1] == 0 && header[2] == 0) {
		return true;
	}

	//! @todo We may have written things other than CV_32F and CV_64F.
	if (header[0] == 4) {
		temp.create(static_cast<int>(header[1]), static_cast<int>(header[2]), CV_32F);
	} else {
		temp.create(static_cast<int>(header[1]), static_cast<int>(header[2]), CV_64F);
	}
	read = fread(static_cast<void *>(temp.data), header[0], header[1] * header[2], f);
	if (read != (header[1] * header[2])) {
		U_LOG_E("Failed to read mat body: '%i' '%s'", (int)read, name);
		return false;
	}
	if (m->empty()) {
		m->create(header[1], header[2], temp.type());
	}
	if (temp.type() != m->type()) {
		U_LOG_E("Mat body type does not match: %i vs %i for '%s'", (int)temp.type(), (int)m->type(), name);
		return false;
	}
	if (temp.total() != m->total()) {
		U_LOG_E("Mat total size does not match: %i vs %i for '%s'", (int)temp.total(), (int)m->total(), name);
		return false;
	}
	if (temp.size() == m->size()) {
		// Exact match
		temp.copyTo(*m);
		return true;
	}
	if (temp.size().width == m->size().height && temp.size().height == m->size().width) {
		U_LOG_W("Mat transposing on load: '%s'", name);
		// needs transpose
		cv::transpose(temp, *m);
		return true;
	}
	// highly unlikely so minimally-helpful error message.
	U_LOG_E("Mat dimension unknown mismatch: '%s'", name);
	return false;
}
