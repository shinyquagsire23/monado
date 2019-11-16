// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Handling of files and calibration data.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#include "t_calibration_opencv.h"


/*
 *
 * Pre-declar functions.
 *
 */

static int
mkpath(char *path);

static bool
read_cv_mat(FILE *f, cv::Mat *m, const char *name);

static bool
write_cv_mat(FILE *f, cv::Mat *m);


/*
 *
 * Free functions.
 *
 */

extern "C" void
t_settings_stereo_free(struct t_settings_stereo **data_ptr)
{
	CalibrationData *cd = (CalibrationData *)*data_ptr;
	if (cd == NULL) {
		return;
	}

	delete cd;
	*data_ptr = NULL;
}

extern "C" void
t_settings_stereo_raw_free(struct t_settings_stereo_raw **raw_data_ptr)
{
	CalibrationRawData *crd = (CalibrationRawData *)*raw_data_ptr;
	if (crd == NULL) {
		return;
	}

	delete crd;
	*raw_data_ptr = NULL;
}


/*
 *
 * Load functions.
 *
 */

extern "C" bool
t_settings_stereo_load_v1(FILE *calib_file,
                          struct t_settings_stereo **out_data,
                          struct t_settings_stereo_raw **out_raw_data)
{
	CalibrationRawData &raw = *(new CalibrationRawData());
	CalibrationData &data = *(new CalibrationData());

	assert(raw.isDataStorageValid());

	// Dummies
	cv::Mat l_translation_dummy;
	cv::Mat r_translation_dummy;

	//! @todo Load from file.
	bool use_fisheye = false;

	// Read our calibration from this file
	// clang-format off
	read_cv_mat(calib_file, &raw.l_intrinsics_mat, "l_intrinsics"); // 3 x 3
	read_cv_mat(calib_file, &raw.r_intrinsics_mat, "r_intrinsics"); // 3 x 3
	read_cv_mat(calib_file, &raw.l_distortion_mat, "l_distortion"); // 1 x 5
	read_cv_mat(calib_file, &raw.r_distortion_mat, "r_distortion"); // 1 x 5
	read_cv_mat(calib_file, &raw.l_distortion_fisheye_mat, "l_distortion_fisheye");
	read_cv_mat(calib_file, &raw.r_distortion_fisheye_mat, "r_distortion_fisheye");
	read_cv_mat(calib_file, &raw.l_rotation_mat, "l_rotation"); // 3 x 3
	read_cv_mat(calib_file, &raw.r_rotation_mat, "r_rotation"); // 3 x 3
	read_cv_mat(calib_file, &l_translation_dummy, "l_translation"); // empty
	read_cv_mat(calib_file, &r_translation_dummy, "r_translation"); // empty
	read_cv_mat(calib_file, &raw.l_projection_mat, "l_projection"); // 3 x 4
	read_cv_mat(calib_file, &raw.r_projection_mat, "r_projection"); // 3 x 4
	read_cv_mat(calib_file, &raw.disparity_to_depth_mat, "disparity_to_depth");  // 4 x 4
	cv::Mat mat_image_size = {};
	read_cv_mat(calib_file, &mat_image_size, "mat_image_size");

	raw.image_size_pixels.w = uint32_t(mat_image_size.at<float>(0, 0));
	raw.image_size_pixels.h = uint32_t(mat_image_size.at<float>(0, 1));
	cv::Size image_size(raw.image_size_pixels.w, raw.image_size_pixels.h);

	cv::Mat mat_new_image_size = {};
	if (read_cv_mat(calib_file, &mat_new_image_size, "mat_new_image_size")) {
		raw.new_image_size_pixels.w = uint32_t(mat_new_image_size.at<float>(0, 0));
		raw.new_image_size_pixels.h = uint32_t(mat_new_image_size.at<float>(0, 1));
	} else {
		raw.new_image_size_pixels.w = raw.image_size_pixels.w;
		raw.new_image_size_pixels.h = raw.image_size_pixels.h;
	}

	if (!read_cv_mat(calib_file, &raw.camera_translation_mat, "translation")) {
		fprintf(stderr, "\tRe-run calibration!\n");
	}
	if (!read_cv_mat(calib_file, &raw.camera_rotation_mat, "rotation")) {
		fprintf(stderr, "\tRe-run calibration!\n");
	}
	if (!read_cv_mat(calib_file, &raw.camera_essential_mat, "essential")) {
		fprintf(stderr, "\tRe-run calibration!\n");
	}
	if (!read_cv_mat(calib_file, &raw.camera_fundamental_mat, "fundamental")) {
		fprintf(stderr, "\tRe-run calibration!\n");
	}
	// clang-format on

	if (raw.camera_translation_mat.size() == cv::Size(3, 1)) {
		fprintf(stderr,
		        "Radjusting translation, re-run calibration.\n");
		raw.camera_translation[0] =
		    raw.camera_translation_mat.at<double>(0, 0);
		raw.camera_translation[1] =
		    raw.camera_translation_mat.at<double>(0, 1);
		raw.camera_translation[2] =
		    raw.camera_translation_mat.at<double>(0, 2);
		raw.camera_translation_mat =
		    cv::Mat(3, 1, CV_64F, &raw.camera_translation[0]);
	}

	assert(raw.isDataStorageValid());

	// No processing needed.
	data.image_size_pixels = raw.image_size_pixels;
	data.new_image_size_pixels = raw.new_image_size_pixels;
	data.disparity_to_depth = raw.disparity_to_depth_mat.clone();

	//! @todo Scale Our intrinsics if the frame size we request
	//              calibration for does not match what was saved

	// Generate undistortion maps - handle fisheye or rectilinear sources

	if (use_fisheye) {
		cv::fisheye::initUndistortRectifyMap(
		    raw.l_intrinsics_mat,         // cameraMatrix
		    raw.l_distortion_fisheye_mat, // distCoeffs
		    cv::noArray(),                // R
		    raw.l_intrinsics_mat,         // newCameraMatrix
		    image_size,                   // size
		    CV_32FC1,                     // m1type
		    data.l_undistort_map_x,       // map1
		    data.l_undistort_map_y);      // map2
		cv::fisheye::initUndistortRectifyMap(
		    raw.r_intrinsics_mat,         // cameraMatrix
		    raw.r_distortion_fisheye_mat, // distCoeffs
		    cv::noArray(),                // R
		    raw.r_intrinsics_mat,         // newCameraMatrix
		    image_size,                   // size
		    CV_32FC1,                     // m1type
		    data.r_undistort_map_x,       // map1
		    data.r_undistort_map_y);      // map2
	} else {
		cv::initUndistortRectifyMap(
		    raw.l_intrinsics_mat,    // cameraMatrix
		    raw.l_distortion_mat,    // distCoeffs
		    cv::noArray(),           // R
		    raw.l_intrinsics_mat,    // newCameraMatrix
		    image_size,              // size
		    CV_32FC1,                // m1type
		    data.l_undistort_map_x,  // map1
		    data.l_undistort_map_y); // map2
		cv::initUndistortRectifyMap(
		    raw.r_intrinsics_mat,    // cameraMatrix
		    raw.r_distortion_mat,    // distCoeffs
		    cv::noArray(),           // R
		    raw.r_intrinsics_mat,    // newCameraMatrix
		    image_size,              // size
		    CV_32FC1,                // m1type
		    data.r_undistort_map_x,  // map1
		    data.r_undistort_map_y); // map2
	}

	/*
	 * Generate our rectification maps
	 *
	 * Here cv::noArray() means zero distortion.
	 */

	cv::initUndistortRectifyMap(raw.l_intrinsics_mat,  // cameraMatrix
	                            cv::noArray(),         // distCoeffs
	                            raw.l_rotation_mat,    // R
	                            raw.l_projection_mat,  // newCameraMatrix
	                            image_size,            // size
	                            CV_32FC1,              // m1type
	                            data.l_rectify_map_x,  // map1
	                            data.l_rectify_map_y); // map2
	cv::initUndistortRectifyMap(raw.r_intrinsics_mat,  // cameraMatrix
	                            cv::noArray(),         // distCoeffs
	                            raw.r_rotation_mat,    // R
	                            raw.r_projection_mat,  // newCameraMatrix
	                            image_size,            // size
	                            CV_32FC1,              // m1type
	                            data.r_rectify_map_x,  // map1
	                            data.r_rectify_map_y); // map2

	*out_data = &data;
	*out_raw_data = &raw;

	return true;
}


/*
 *
 * Save functions.
 *
 */

extern "C" bool
t_file_save_raw_data(FILE *calib_file, struct t_settings_stereo_raw *raw_data)
{
	CalibrationRawData &raw = *(CalibrationRawData *)raw_data;

	cv::Mat l_translation_dummy;
	cv::Mat r_translation_dummy;


	write_cv_mat(calib_file, &raw.l_intrinsics_mat);
	write_cv_mat(calib_file, &raw.r_intrinsics_mat);
	write_cv_mat(calib_file, &raw.l_distortion_mat);
	write_cv_mat(calib_file, &raw.r_distortion_mat);
	write_cv_mat(calib_file, &raw.l_distortion_fisheye_mat);
	write_cv_mat(calib_file, &raw.r_distortion_fisheye_mat);
	write_cv_mat(calib_file, &raw.l_rotation_mat);
	write_cv_mat(calib_file, &raw.r_rotation_mat);
	write_cv_mat(calib_file, &l_translation_dummy);
	write_cv_mat(calib_file, &r_translation_dummy);
	write_cv_mat(calib_file, &raw.l_projection_mat);
	write_cv_mat(calib_file, &raw.r_projection_mat);
	write_cv_mat(calib_file, &raw.disparity_to_depth_mat);

	cv::Mat mat_image_size;
	mat_image_size.create(1, 2, CV_32F);
	mat_image_size.at<float>(0, 0) = raw.image_size_pixels.w;
	mat_image_size.at<float>(0, 1) = raw.image_size_pixels.h;
	write_cv_mat(calib_file, &mat_image_size);

	cv::Mat mat_new_image_size;
	mat_new_image_size.create(1, 2, CV_32F);
	mat_new_image_size.at<float>(0, 0) = raw.new_image_size_pixels.w;
	mat_new_image_size.at<float>(0, 1) = raw.new_image_size_pixels.h;
	write_cv_mat(calib_file, &mat_new_image_size);

	write_cv_mat(calib_file, &raw.camera_translation_mat);
	write_cv_mat(calib_file, &raw.camera_rotation_mat);
	write_cv_mat(calib_file, &raw.camera_essential_mat);
	write_cv_mat(calib_file, &raw.camera_fundamental_mat);

	return true;
}


/*
 *
 * Hack functions.
 *
 */

extern "C" bool
t_settings_stereo_load_v1_hack(struct t_settings_stereo **out_data)
{
	const char *configuration_filename = "PS4_EYE";

	char path_string[256]; //! @todo 256 maybe not enough
	//! @todo Use multiple env vars?
	char *config_path = secure_getenv("HOME");
	snprintf(path_string, 256, "%s/.config/monado/%s.calibration",
	         config_path, configuration_filename); //! @todo Hardcoded 256

	FILE *calib_file = fopen(path_string, "rb");
	if (calib_file == NULL) {
		return false;
	}

	t_settings_stereo_raw *raw_data;
	bool ret = t_settings_stereo_load_v1(calib_file, out_data, &raw_data);

	t_settings_stereo_raw_free(&raw_data);

	fclose(calib_file);

	return ret;
}

extern "C" bool
t_file_save_raw_data_hack(struct t_settings_stereo_raw *raw_data)
{
	char path_string[PATH_MAX];
	char file_string[PATH_MAX];
	// TODO: centralise this - use multiple env vars?
	char *config_path = secure_getenv("HOME");
	snprintf(path_string, PATH_MAX, "%s/.config/monado", config_path);
	snprintf(file_string, PATH_MAX, "%s/.config/monado/%s.calibration",
	         config_path, "PS4_EYE");
	FILE *calib_file = fopen(file_string, "wb");
	if (!calib_file) {
		// try creating it
		mkpath(path_string);
	}
	calib_file = fopen(file_string, "wb");
	if (!calib_file) {
		printf(
		    "ERROR. could not create calibration file "
		    "%s\n",
		    file_string);
		return false;
	}

	t_file_save_raw_data(calib_file, raw_data);

	fclose(calib_file);

	return true;
}


/*
 *
 * Helpers
 *
 */

//! @todo Move this as it is a generic helper
static int
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

static bool
write_cv_mat(FILE *f, cv::Mat *m)
{
	uint32_t header[3];
	header[0] = static_cast<uint32_t>(m->elemSize());
	header[1] = static_cast<uint32_t>(m->rows);
	header[2] = static_cast<uint32_t>(m->cols);
	fwrite(static_cast<void *>(header), sizeof(uint32_t), 3, f);
	fwrite(static_cast<void *>(m->data), header[0], header[1] * header[2],
	       f);
	return true;
}

static bool
read_cv_mat(FILE *f, cv::Mat *m, const char *name)
{
	uint32_t header[3] = {};
	size_t read = 0;

	read = fread(static_cast<void *>(header), sizeof(uint32_t), 3, f);
	if (read != 3) {
		printf("Failed to read mat header: '%i' '%s'\n", (int)read,
		       name);
		return false;
	}

	if (header[1] == 0 && header[2] == 0) {
		return true;
	}

	//! @todo We may have written things other than CV_32F and CV_64F.
	if (header[0] == 4) {
		m->create(static_cast<int>(header[1]),
		          static_cast<int>(header[2]), CV_32F);
	} else {
		m->create(static_cast<int>(header[1]),
		          static_cast<int>(header[2]), CV_64F);
	}
	read = fread(static_cast<void *>(m->data), header[0],
	             header[1] * header[2], f);
	if (read != (header[1] * header[2])) {
		printf("Failed to read mat body: '%i' '%s'\n", (int)read, name);
		return false;
	}

	return true;
}
