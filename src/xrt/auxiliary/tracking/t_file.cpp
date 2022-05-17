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
#include "util/u_json.hpp"
#include "os/os_time.h"


DEBUG_GET_ONCE_LOG_OPTION(calib_log, "CALIB_LOG", U_LOGGING_INFO)

#define CALIB_TRACE(...) U_LOG_IFL_T(debug_get_log_option_calib_log(), __VA_ARGS__)
#define CALIB_DEBUG(...) U_LOG_IFL_D(debug_get_log_option_calib_log(), __VA_ARGS__)
#define CALIB_INFO(...) U_LOG_IFL_I(debug_get_log_option_calib_log(), __VA_ARGS__)
#define CALIB_WARN(...) U_LOG_IFL_W(debug_get_log_option_calib_log(), __VA_ARGS__)
#define CALIB_ERROR(...) U_LOG_IFL_E(debug_get_log_option_calib_log(), __VA_ARGS__)
#define CALIB_ASSERT(predicate, ...)                                                                                   \
	do {                                                                                                           \
		bool p = predicate;                                                                                    \
		if (!p) {                                                                                              \
			U_LOG(U_LOGGING_ERROR, __VA_ARGS__);                                                           \
			assert(false && "CALIB_ASSERT failed: " #predicate);                                           \
			exit(EXIT_FAILURE);                                                                            \
		}                                                                                                      \
	} while (false);
#define CALIB_ASSERT_(predicate) CALIB_ASSERT(predicate, "Assertion failed " #predicate)

// Return assert
#define CALIB_ASSERTR(predicate, ...)                                                                                  \
	if (!(predicate)) {                                                                                            \
		U_LOG(U_LOGGING_ERROR, __VA_ARGS__);                                                                   \
		return false;                                                                                          \
	}

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
namespace xrt::auxiliary::tracking {
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
	CALIB_ASSERT_(data != NULL);
	CALIB_ASSERT_(data->view[0].image_size_pixels.w == data->view[1].image_size_pixels.w);
	CALIB_ASSERT_(data->view[0].image_size_pixels.h == data->view[1].image_size_pixels.h);

	CALIB_ASSERT_(data->view[0].use_fisheye == data->view[1].use_fisheye);

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
		double balance = 0.0; // also known as alpha.
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
} // namespace xrt::auxiliary::tracking

using std::array;
using std::string;
using std::vector;
using xrt::auxiliary::tracking::CameraCalibrationWrapper;
using xrt::auxiliary::tracking::StereoCameraCalibrationWrapper;
using xrt::auxiliary::util::json::JSONBuilder;
using xrt::auxiliary::util::json::JSONNode;

/*
 *
 * Load functions.
 *
 */

extern "C" bool
t_stereo_camera_calibration_load_v1(FILE *calib_file, struct t_stereo_camera_calibration **out_data)
{

	t_stereo_camera_calibration *data_ptr = NULL;
	t_stereo_camera_calibration_alloc(&data_ptr, 5); // Hardcoded to 5 distortion parameters.
	StereoCameraCalibrationWrapper wrapped(data_ptr);

	// Scratch-space temporary matrix
	cv::Mat scratch;

	// Read our calibration from this file
	// clang-format off
	cv::Mat_<float> mat_image_size(2, 1);
	bool result = read_cv_mat(calib_file, &wrapped.view[0].intrinsics_mat, "l_intrinsics"); // 3 x 3
	result = result && read_cv_mat(calib_file, &wrapped.view[1].intrinsics_mat, "r_intrinsics"); // 3 x 3
	result = result && read_cv_mat(calib_file, &wrapped.view[0].distortion_mat, "l_distortion"); // 5 x 1
	result = result && read_cv_mat(calib_file, &wrapped.view[1].distortion_mat, "r_distortion"); // 5 x 1
	result = result && read_cv_mat(calib_file, &wrapped.view[0].distortion_fisheye_mat, "l_distortion_fisheye"); // 4 x 1
	result = result && read_cv_mat(calib_file, &wrapped.view[1].distortion_fisheye_mat, "r_distortion_fisheye"); // 4 x 1
	result = result && read_cv_mat(calib_file, &scratch, "l_rotation"); // 3 x 3
	result = result && read_cv_mat(calib_file, &scratch, "r_rotation"); // 3 x 3
	result = result && read_cv_mat(calib_file, &scratch, "l_translation"); // empty
	result = result && read_cv_mat(calib_file, &scratch, "r_translation"); // empty
	result = result && read_cv_mat(calib_file, &scratch, "l_projection"); // 3 x 4
	result = result && read_cv_mat(calib_file, &scratch, "r_projection"); // 3 x 4
	result = result && read_cv_mat(calib_file, &scratch, "disparity_to_depth");  // 4 x 4
	result = result && read_cv_mat(calib_file, &mat_image_size, "mat_image_size");

	if (!result) {
		CALIB_WARN("Re-run calibration!");
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
		CALIB_WARN("Re-run calibration!");
	}
	if (!read_cv_mat(calib_file, &wrapped.camera_rotation_mat, "rotation")) {
		CALIB_WARN("Re-run calibration!");
	}
	if (!read_cv_mat(calib_file, &wrapped.camera_essential_mat, "essential")) {
		CALIB_WARN("Re-run calibration!");
	}
	if (!read_cv_mat(calib_file, &wrapped.camera_fundamental_mat, "fundamental")) {
		CALIB_WARN("Re-run calibration!");
	}

	cv::Mat_<float> mat_use_fisheye(1, 1);
	if (!read_cv_mat(calib_file, &mat_use_fisheye, "use_fisheye")) {
		wrapped.view[0].use_fisheye = false;
		CALIB_WARN("Re-run calibration! (Assuming not fisheye)");
	} else {
		wrapped.view[0].use_fisheye = mat_use_fisheye(0, 0) != 0.0f;
	}
	wrapped.view[1].use_fisheye = wrapped.view[0].use_fisheye;
	// clang-format on

	CALIB_ASSERT_(wrapped.isDataStorageValid());

	t_stereo_camera_calibration_reference(out_data, data_ptr);
	t_stereo_camera_calibration_reference(&data_ptr, NULL);

	return true;
}

static bool
t_stereo_camera_calibration_load_path_v1(const char *calib_path, struct t_stereo_camera_calibration **out_data)
{
	CALIB_WARN("Deprecated function %s", __func__);

	FILE *calib_file = fopen(calib_path, "rb");
	if (calib_file == nullptr) {
		CALIB_ERROR("Unable to open calibration file: '%s'", calib_path);
		return false;
	}

	bool success = t_stereo_camera_calibration_load_v1(calib_file, out_data);
	fclose(calib_file);

	return success;
}

#define PINHOLE_RADTAN5 "pinhole_radtan5"
#define FISHEYE_EQUIDISTANT4 "fisheye_equidistant4"

//! Fills @p out_mat from a json array stored in @p jn. Returns true if @p jn is
//! a valid @p rows * @p cols matrix, false otherwise.
static bool
load_mat_field(const JSONNode &jn, int rows, int cols, cv::Mat_<double> &out_mat)
{
	vector<JSONNode> data = jn.asArray();
	bool valid = jn.isArray() && data.size() == static_cast<size_t>(rows * cols);

	if (valid) {
		out_mat.create(rows, cols);
		for (int i = 0; i < rows * cols; i++) {
			out_mat(i) = data[i].asDouble();
		}
	} else {
		CALIB_WARN("Invalid '%s' matrix field", jn.getName().c_str());
	}

	return valid;
}

/*!
 * Overload of @ref load_mat_field that saves the result into a 2D C-array.
 */
template <int rows, int cols>
XRT_MAYBE_UNUSED static bool
load_mat_field(const JSONNode &jn, double (&out_arr)[rows][cols])
{
	cv::Mat_<double> cvmat{rows, cols, &out_arr[0][0]}; // Wraps out_arr address
	return load_mat_field(jn, rows, cols, cvmat);
}

/*!
 * Overload of @ref load_mat_field that saves the result into a 1D C-array.
 */
template <int dim>
XRT_MAYBE_UNUSED static bool
load_mat_field(const JSONNode &jn, double (&out_arr)[dim])
{
	cv::Mat_<double> cvmat{dim, 1, &out_arr[0]}; // Wraps out_arr address
	return load_mat_field(jn, dim, 1, cvmat);
}

static bool
t_camera_calibration_load_v2(cJSON *cjson_cam, t_camera_calibration *cc)
{
	JSONNode jc{cjson_cam};

	string model = jc["model"].asString();
	memset(&cc->intrinsics, 0, sizeof(cc->intrinsics));
	cc->intrinsics[0][0] = jc["intrinsics"]["fx"].asDouble();
	cc->intrinsics[1][1] = jc["intrinsics"]["fy"].asDouble();
	cc->intrinsics[0][2] = jc["intrinsics"]["cx"].asDouble();
	cc->intrinsics[1][2] = jc["intrinsics"]["cy"].asDouble();
	cc->intrinsics[2][2] = 1;

	size_t n = jc["distortion"].asObject().size();
	if (model == PINHOLE_RADTAN5) {
		cc->use_fisheye = false;
		CALIB_ASSERTR(n == 5, "%zu != 5 distortion params", n);

		constexpr array names{"k1", "k2", "p1", "p2", "k3"};
		for (size_t i = 0; i < n; i++) {
			cc->distortion[i] = jc["distortion"][names[i]].asDouble();
		}
	} else if (model == FISHEYE_EQUIDISTANT4) {
		cc->use_fisheye = true;
		CALIB_ASSERTR(n == 4, "%zu != 4 distortion params", n);

		constexpr array names{"k1", "k2", "k3", "k4"};
		for (size_t i = 0; i < n; i++) {
			cc->distortion_fisheye[i] = jc["distortion"][names[i]].asDouble();
		}
	} else {
		CALIB_ASSERTR(false, "Invalid camera model: '%s'", model.c_str());
		return false;
	}

	cc->image_size_pixels.w = jc["resolution"]["width"].asInt();
	cc->image_size_pixels.h = jc["resolution"]["height"].asInt();
	return true;
}

extern "C" bool
t_stereo_camera_calibration_from_json_v2(cJSON *cjson, struct t_stereo_camera_calibration **out_stereo)
{
	JSONNode json{cjson};
	StereoCameraCalibrationWrapper stereo{5}; // Hardcoded to 5 distortion parameters.

	// Load file metadata
	const int supported_version = 2;
	int version = json["metadata"]["version"].asInt(supported_version);
	if (json["metadata"]["version"].isInvalid()) {
		CALIB_WARN("'metadata.version' not found, will assume version=%d", supported_version);
	}
	CALIB_ASSERTR(version == supported_version, "Calibration json version (%d) != %d", version, supported_version);

	// Load cameras
	vector<JSONNode> cameras = json["cameras"].asArray();
	bool okmats = true;
	CALIB_ASSERTR(cameras.size() == 2, "Two cameras must be specified, %zu given", cameras.size());
	for (size_t i = 0; i < cameras.size(); i++) {
		JSONNode jc = cameras[i];
		CameraCalibrationWrapper &cc = stereo.view[i];
		bool loaded = t_camera_calibration_load_v2(jc.getCJSON(), &cc.base);
		CALIB_ASSERTR(loaded, "Unable to load camera calibration: %s", jc.toString(false).c_str());
	}

	JSONNode rel = json["opencv_stereo_calibrate"];
	okmats &= load_mat_field(rel["rotation"], 3, 3, stereo.camera_rotation_mat);
	okmats &= load_mat_field(rel["translation"], 3, 1, stereo.camera_translation_mat);
	okmats &= load_mat_field(rel["essential"], 3, 3, stereo.camera_essential_mat);
	okmats &= load_mat_field(rel["fundamental"], 3, 3, stereo.camera_fundamental_mat);

	CALIB_ASSERTR(okmats, "One or more calibration matrices couldn't be loaded");
	CALIB_ASSERT_(stereo.isDataStorageValid());

	t_stereo_camera_calibration_reference(out_stereo, stereo.base);

	return true;
}

static bool
t_stereo_camera_calibration_load_path_v2(const char *calib_path, struct t_stereo_camera_calibration **out_stereo)
{
	JSONNode json = JSONNode::loadFromFile(calib_path);
	if (json.isInvalid()) {
		CALIB_ERROR("Unable to open calibration file: '%s'", calib_path);
		return false;
	}
	return t_stereo_camera_calibration_from_json_v2(json.getCJSON(), out_stereo);
}


/*
 *
 * Save functions.
 *
 */

extern "C" bool
t_stereo_camera_calibration_save_v1(FILE *calib_file, struct t_stereo_camera_calibration *data)
{
	CALIB_WARN("Deprecated function: %s", __func__);

	StereoCameraCalibrationWrapper wrapped(data);
	// Scratch-space temporary matrix
	cv::Mat scratch;

	write_cv_mat(calib_file, &wrapped.view[0].intrinsics_mat);
	write_cv_mat(calib_file, &wrapped.view[1].intrinsics_mat);
	write_cv_mat(calib_file, &wrapped.view[0].distortion_mat);
	write_cv_mat(calib_file, &wrapped.view[1].distortion_mat);
	write_cv_mat(calib_file, &wrapped.view[0].distortion_fisheye_mat);
	write_cv_mat(calib_file, &wrapped.view[1].distortion_fisheye_mat);
	write_cv_mat(calib_file, &scratch); // view[0].rotation_mat
	write_cv_mat(calib_file, &scratch); // view[1].rotation_mat
	write_cv_mat(calib_file, &scratch); // l_translation
	write_cv_mat(calib_file, &scratch); // r_translation
	write_cv_mat(calib_file, &scratch); // view[0].projection_mat
	write_cv_mat(calib_file, &scratch); // view[1].projection_mat
	write_cv_mat(calib_file, &scratch); // disparity_to_depth_mat

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

static bool
t_stereo_camera_calibration_save_path_v1(const char *calib_path, struct t_stereo_camera_calibration *data)
{
	FILE *calib_file = fopen(calib_path, "wb");
	if (calib_file == nullptr) {
		CALIB_ERROR("Unable to open calibration file: '%s'", calib_path);
		return false;
	}

	bool success = t_stereo_camera_calibration_save_v1(calib_file, data);
	fclose(calib_file);

	return success;
}

//! Writes @p mat data into a @p jb as a json array.
static JSONBuilder &
operator<<(JSONBuilder &jb, const cv::Mat_<double> &mat)
{
	jb << "[";
	for (int i = 0; i < mat.rows * mat.cols; i++) {
		jb << mat.at<double>(i);
	}
	jb << "]";
	return jb;
}

extern "C" bool
t_stereo_camera_calibration_to_json_v2(cJSON **out_cjson, struct t_stereo_camera_calibration *data)
{
	StereoCameraCalibrationWrapper wrapped(data);
	JSONBuilder jb{};

	jb << "{";
	jb << "$schema"
	   << "https://monado.pages.freedesktop.org/monado/calibration_v2.schema.json";
	jb << "metadata";
	jb << "{";
	jb << "version" << 2;
	jb << "}";

	jb << "cameras";
	jb << "[";

	// Cameras
	for (size_t i = 0; i < 2; i++) {
		const auto &view = wrapped.view[i];
		jb << "{";
		jb << "model" << (view.use_fisheye ? FISHEYE_EQUIDISTANT4 : PINHOLE_RADTAN5);

		jb << "intrinsics";
		jb << "{";
		jb << "fx" << view.intrinsics_mat(0, 0);
		jb << "fy" << view.intrinsics_mat(1, 1);
		jb << "cx" << view.intrinsics_mat(0, 2);
		jb << "cy" << view.intrinsics_mat(1, 2);
		jb << "}";

		jb << "distortion";
		jb << "{";
		if (view.use_fisheye) {
			int n = view.distortion_fisheye_mat.size().area(); // Number of distortion parameters
			CALIB_ASSERT_(n == 4);

			constexpr array names{"k1", "k2", "k3", "k4"};
			for (int i = 0; i < n; i++) {
				jb << names[i] << view.distortion_fisheye_mat(i);
			}
		} else {
			int n = view.distortion_mat.size().area(); // Number of distortion parameters
			CALIB_ASSERT_(n == 5);

			constexpr array names{"k1", "k2", "p1", "p2", "k3"};
			for (int i = 0; i < n; i++) {
				jb << names[i] << view.distortion_mat(i);
			}
		}
		jb << "}";

		jb << "resolution";
		jb << "{";
		jb << "width" << view.image_size_pixels.w;
		jb << "height" << view.image_size_pixels.h;
		jb << "}";

		jb << "}";
	}

	jb << "]";

	// cv::stereoCalibrate data
	jb << "opencv_stereo_calibrate"
	   << "{";
	jb << "rotation" << wrapped.camera_rotation_mat;
	jb << "translation" << wrapped.camera_translation_mat;
	jb << "essential" << wrapped.camera_essential_mat;
	jb << "fundamental" << wrapped.camera_fundamental_mat;
	jb << "}";

	jb << "}";

	cJSON *cjson = jb.getBuiltNode()->getCJSON();
	*out_cjson = cJSON_Duplicate(cjson, true);
	return true;
}

static bool
t_stereo_camera_calibration_save_path_v2(const char *calib_path, struct t_stereo_camera_calibration *data)
{
	cJSON *cjson = NULL;
	bool success = t_stereo_camera_calibration_to_json_v2(&cjson, data);
	if (!success) {
		return false;
	}

	JSONNode json{cjson, true, nullptr}; // is_owner=true so it will free cjson object when leaving scope
	CALIB_INFO("Saving calibration file: %s", json.toString(false).c_str());
	return json.saveToFile(calib_path);
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
		CALIB_ERROR("Failed to read mat header: '%i' '%s'", (int)read, name);
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
		CALIB_ERROR("Failed to read mat body: '%i' '%s'", (int)read, name);
		return false;
	}
	if (m->empty()) {
		m->create(header[1], header[2], temp.type());
	}
	if (temp.type() != m->type()) {
		CALIB_ERROR("Mat body type does not match: %i vs %i for '%s'", (int)temp.type(), (int)m->type(), name);
		return false;
	}
	if (temp.total() != m->total()) {
		CALIB_ERROR("Mat total size does not match: %i vs %i for '%s'", (int)temp.total(), (int)m->total(),
		            name);
		return false;
	}
	if (temp.size() == m->size()) {
		// Exact match
		temp.copyTo(*m);
		return true;
	}
	if (temp.size().width == m->size().height && temp.size().height == m->size().width) {
		CALIB_WARN("Mat transposing on load: '%s'", name);
		// needs transpose
		cv::transpose(temp, *m);
		return true;
	}
	// highly unlikely so minimally-helpful error message.
	CALIB_ERROR("Mat dimension unknown mismatch: '%s'", name);
	return false;
}

static bool
has_json_extension(const char *filename)
{
	const char extension[] = ".json";
	size_t name_len = strlen(filename);
	size_t ext_len = strlen(extension);

	if (name_len > ext_len) {
		return strcmp(&filename[name_len - ext_len], extension) == 0;
	}

	return false;
}


/*
 *
 * Exported functions
 *
 */

extern "C" bool
t_stereo_camera_calibration_load(const char *calib_path, struct t_stereo_camera_calibration **out_data)
{
	return has_json_extension(calib_path) ? t_stereo_camera_calibration_load_path_v2(calib_path, out_data)
	                                      : t_stereo_camera_calibration_load_path_v1(calib_path, out_data);
}

extern "C" bool
t_stereo_camera_calibration_save(const char *calib_path, struct t_stereo_camera_calibration *data)
{
	return has_json_extension(calib_path) ? t_stereo_camera_calibration_save_path_v2(calib_path, data)
	                                      : t_stereo_camera_calibration_save_path_v1(calib_path, data);
}
