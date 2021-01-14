// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Calibration code.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_tracking
 */

#include "util/u_sink.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_frame.h"
#include "util/u_format.h"

#include "tracking/t_tracking.h"
#include "tracking/t_calibration_opencv.hpp"

#include <opencv2/opencv.hpp>
#include <sys/stat.h>
#include <utility>


DEBUG_GET_ONCE_BOOL_OPTION(hsv_filter, "T_DEBUG_HSV_FILTER", false)
DEBUG_GET_ONCE_BOOL_OPTION(hsv_picker, "T_DEBUG_HSV_PICKER", false)
DEBUG_GET_ONCE_BOOL_OPTION(hsv_viewer, "T_DEBUG_HSV_VIEWER", false)


/*
 *
 * Structs
 *
 */

//! Model of the thing we are measuring to calibrate, 32 bit.
typedef std::vector<cv::Point3f> ModelF32;
//! Model of the thing we are measuring to calibrate, 64 bit.
typedef std::vector<cv::Point3d> ModelF64;
//! A measurement of the model as viewed on the camera.
typedef std::vector<cv::Point2f> MeasurementF32;
//! In doubles, because OpenCV can't agree on a single type to use.
typedef std::vector<cv::Point2d> MeasurementF64;
//! For each @ref MeasurementF32 we take we also save the @ref ModelF32.
typedef std::vector<ModelF32> ArrayOfModelF32s;
//! For each @ref MeasurementF64 we take we also save the @ref ModelF64.
typedef std::vector<ModelF64> ArrayOfModelF64s;
//! A array of @ref MeasurementF32.
typedef std::vector<MeasurementF32> ArrayOfMeasurementF32s;
//! A array of @ref MeasurementF64.
typedef std::vector<MeasurementF64> ArrayOfMeasurementF64s;
//! A array of bounding rects.
typedef std::vector<cv::Rect> ArrayOfRects;

/*!
 * Current state for each view, one view for mono cameras, two for stereo.
 */
struct ViewState
{
	ArrayOfMeasurementF32s measured_f32 = {};
	ArrayOfMeasurementF64s measured_f64 = {};
	ArrayOfRects measured_bounds = {};

	bool last_valid = false;
	MeasurementF64 last = {};

	MeasurementF64 current_f64 = {};
	MeasurementF32 current_f32 = {};
	cv::Rect current_bounds = {};

	cv::Rect pre_rect = {};
	cv::Rect post_rect = {};

	bool maps_valid = false;
	cv::Mat map1 = {};
	cv::Mat map2 = {};
};

/*!
 * Main class for doing calibration.
 */
class Calibration
{
public:
	struct xrt_frame_sink base = {};

	struct
	{
		cv::Mat rgb = {};
		struct xrt_frame *frame = {};
		struct xrt_frame_sink *sink = {};
	} gui;

	struct
	{
		ModelF32 model_f32 = {};
		ModelF64 model_f64 = {};
		cv::Size dims = {8, 6};
		enum t_board_pattern pattern = T_BOARD_CHECKERS;
		float spacing_meters = 0.05;
	} board;

	struct
	{
		ViewState view[2] = {};

		ArrayOfModelF32s board_models_f32 = {};
		ArrayOfModelF64s board_models_f64 = {};

		uint32_t calibration_count = {};
		bool calibrated = false;


		uint32_t cooldown = 0;
		uint32_t waited_for = 0;
		uint32_t collected_of_part = 0;
	} state;

	struct
	{
		bool enabled = false;
		uint32_t num_images = 20;
	} load;

	//! Should we use subpixel enhancing for checkerboard.
	bool subpixel_enable = true;
	//! What subpixel range for checkerboard enhancement.
	int subpixel_size = 5;

	//! Number of frames to wait for cooldown.
	uint32_t num_cooldown_frames = 20;
	//! Number of frames to wait for before collecting.
	uint32_t num_wait_for = 5;
	//! Total number of samples to collect.
	uint32_t num_collect_total = 20;
	//! Number of frames to capture before restarting.
	uint32_t num_collect_restart = 1;

	//! Is the camera fisheye.
	bool use_fisheye = false;
	//! From parameters.
	bool stereo_sbs = false;

	//! Should we clear the frame.
	bool clear_frame = false;

	//! Dump all of the measurements to stdout.
	bool dump_measurements = false;

	//! Should we save images used for capture.
	bool save_images = false;

	//! Should we mirror the rgb images.
	bool mirror_rgb_image = false;

	cv::Mat gray = {};

	char text[512] = {};

	t_calibration_status *status;
};


/*
 *
 * Small helpers.
 *
 */

static void
to_stdout(const char *name, const cv::Mat &mat)
{
	std::cout << name << " " << mat.size() << ":\n" << mat << "\n";
}

static void
refresh_gui_frame(class Calibration &c, int rows, int cols)
{
	// Also dereferences the old frame.
	u_frame_create_one_off(XRT_FORMAT_R8G8B8, cols, rows, &c.gui.frame);

	c.gui.rgb = cv::Mat(rows, cols, CV_8UC3, c.gui.frame->data, c.gui.frame->stride);
}

static void
send_rgb_frame(class Calibration &c)
{
	c.gui.sink->push_frame(c.gui.sink, c.gui.frame);

	refresh_gui_frame(c, c.gui.rgb.rows, c.gui.rgb.cols);
}

static void
ensure_buffers_are_allocated(class Calibration &c, int rows, int cols)
{
	if (c.gui.rgb.cols == cols && c.gui.rgb.rows == rows) {
		return;
	}

	// If our rgb is not allocated but our gray already is, alloc our rgb
	// now. We will hit this path if we receive L8 format.
	if (c.gray.cols == cols && c.gray.rows == rows) {
		refresh_gui_frame(c, rows, cols);
		return;
	}

	c.gray = cv::Mat(rows, cols, CV_8UC1, cv::Scalar(0));

	refresh_gui_frame(c, rows, cols);
}

static void
print_txt(cv::Mat &rgb, const char *text, double fontScale)
{
	int fontFace = 0;
	int thickness = 2;
	cv::Size textSize = cv::getTextSize(text, fontFace, fontScale, thickness, NULL);

	cv::Point textOrg((rgb.cols - textSize.width) / 2, textSize.height * 2);

	cv::putText(rgb, text, textOrg, fontFace, fontScale, cv::Scalar(192, 192, 192), thickness);
}

static void
make_gui_str(class Calibration &c)
{
	auto &rgb = c.gui.rgb;

	int cols = 800;
	int rows = 100;
	ensure_buffers_are_allocated(c, rows, cols);

	cv::rectangle(rgb, cv::Point2f(0, 0), cv::Point2f(cols, rows), cv::Scalar(0, 0, 0), -1, 0);

	print_txt(rgb, c.text, 1.0);

	send_rgb_frame(c);
}

/*!
 * Simple helper to draw a bounding rect.
 */
static void
draw_rect(cv::Mat &rgb, const cv::Rect &rect, const cv::Scalar &colour)
{
	cv::rectangle(rgb, rect.tl(), rect.br(), colour);
}

static void
do_view_coverage(class Calibration &c, struct ViewState &view, cv::Mat &gray, cv::Mat &rgb, bool found)
{
	// Get the current bounding rect.
	view.current_bounds = cv::boundingRect(view.current_f32);

	// Compute our 'pre sample' coverage for this frame,
	// for display and area threshold checking.
	std::vector<cv::Point2f> coverage;
	coverage.reserve(view.measured_bounds.size() * 2 + 2);
	for (const cv::Rect &brect : view.measured_bounds) {
		draw_rect(rgb, brect, cv::Scalar(0, 64, 32));

		coverage.emplace_back(brect.tl());
		coverage.emplace_back(brect.br());
	}

	// What area of the camera have we calibrated.
	view.pre_rect = cv::boundingRect(coverage);
	draw_rect(rgb, view.pre_rect, cv::Scalar(0, 255, 255));

	if (found) {
		coverage.emplace_back(view.current_bounds.tl());
		coverage.emplace_back(view.current_bounds.br());

		// New area we cover.
		view.post_rect = cv::boundingRect(coverage);

		draw_rect(rgb, view.post_rect, cv::Scalar(0, 255, 0));
	}

	// Draw the checker board, will also draw partial hits.
	cv::drawChessboardCorners(rgb, c.board.dims, view.current_f32, found);
}

static bool
do_view_chess(class Calibration &c, struct ViewState &view, cv::Mat &gray, cv::Mat &rgb)
{
	/*
	 * Fisheye requires measurement and model to be double, other functions
	 * requires them to be floats (like cornerSubPix). So we give in
	 * current_f32 here and convert below.
	 */

	int flags = 0;
	flags += cv::CALIB_CB_FAST_CHECK;
	flags += cv::CALIB_CB_ADAPTIVE_THRESH;
	flags += cv::CALIB_CB_NORMALIZE_IMAGE;

	bool found = cv::findChessboardCorners(gray,             // Image
	                                       c.board.dims,     // patternSize
	                                       view.current_f32, // corners
	                                       flags);           // flags

	// Improve the corner positions.
	if (found && c.subpixel_enable) {
		int crit_flag = 0;
		crit_flag |= cv::TermCriteria::EPS;
		crit_flag |= cv::TermCriteria::COUNT;
		cv::TermCriteria term_criteria = {crit_flag, 30, 0.1};

		cv::Size size(c.subpixel_size, c.subpixel_size);
		cv::Size zero(-1, -1);

		cv::cornerSubPix(gray, view.current_f32, size, zero, term_criteria);
	}

	// Do the conversion here.
	view.current_f64.clear(); // Doesn't effect capacity.
	for (const cv::Point2f &p : view.current_f32) {
		view.current_f64.emplace_back(double(p.x), double(p.y));
	}

	do_view_coverage(c, view, gray, rgb, found);

	return found;
}

static bool
do_view_circles(class Calibration &c, struct ViewState &view, cv::Mat &gray, cv::Mat &rgb)
{
	/*
	 * Fisheye requires measurement and model to be double, other functions
	 * requires them to be floats (like drawChessboardCorners). So we give
	 * in current here for highest precision and convert below.
	 */

	int flags = 0;
	if (c.board.pattern == T_BOARD_ASYMMETRIC_CIRCLES) {
		flags |= cv::CALIB_CB_ASYMMETRIC_GRID;
	}

	bool found = cv::findCirclesGrid(gray,             // Image
	                                 c.board.dims,     // patternSize
	                                 view.current_f64, // corners
	                                 flags);           // flags

	// Convert here so that displaying also works.
	view.current_f32.clear(); // Doesn't effect capacity.
	for (const cv::Point2d &p : view.current_f64) {
		view.current_f32.emplace_back(float(p.x), float(p.y));
	}

	do_view_coverage(c, view, gray, rgb, found);

	return found;
}

static bool
do_view(class Calibration &c, struct ViewState &view, cv::Mat &gray, cv::Mat &rgb)
{
	bool found = false;

	switch (c.board.pattern) {
	case T_BOARD_CHECKERS: //
		found = do_view_chess(c, view, gray, rgb);
		break;
	case T_BOARD_CIRCLES: //
		found = do_view_circles(c, view, gray, rgb);
		break;
	case T_BOARD_ASYMMETRIC_CIRCLES: //
		found = do_view_circles(c, view, gray, rgb);
		break;
	default: assert(false);
	}

	if (c.mirror_rgb_image) {
		cv::flip(rgb, rgb, +1);
	}

	return found;
}

static void
remap_view(class Calibration &c, struct ViewState &view, cv::Mat &rgb)
{
	if (!view.maps_valid) {
		return;
	}

	cv::remap(rgb,                 // src
	          rgb,                 // dst
	          view.map1,           // map1
	          view.map2,           // map2
	          cv::INTER_LINEAR,    // interpolation
	          cv::BORDER_CONSTANT, // borderMode
	          cv::Scalar());       // borderValue
}

static void
build_board_position(class Calibration &c)
{
	int cols_num = c.board.dims.width;
	int rows_num = c.board.dims.height;
	float size_meters = c.board.spacing_meters;

	switch (c.board.pattern) {
	case T_BOARD_CHECKERS:
	case T_BOARD_CIRCLES:
		// Nothing to do.
		break;
	case T_BOARD_ASYMMETRIC_CIRCLES:
		// From diagonal size to "square" size.
		size_meters = sqrt((size_meters * size_meters) / 2.0);
		break;
	}

	switch (c.board.pattern) {
	case T_BOARD_CHECKERS:
	case T_BOARD_CIRCLES:
		c.board.model_f32.reserve(rows_num * cols_num);
		c.board.model_f64.reserve(rows_num * cols_num);
		for (int i = 0; i < rows_num; ++i) {
			for (int j = 0; j < cols_num; ++j) {
				cv::Point3d p = {
				    j * size_meters,
				    i * size_meters,
				    0.0f,
				};
				c.board.model_f32.emplace_back(p);
				c.board.model_f64.emplace_back(p);
			}
		}
		break;
	case T_BOARD_ASYMMETRIC_CIRCLES:
		c.board.model_f32.reserve(rows_num * cols_num);
		c.board.model_f64.reserve(rows_num * cols_num);
		for (int i = 0; i < rows_num; ++i) {
			for (int j = 0; j < cols_num; ++j) {
				cv::Point3d p = {
				    (2 * j + i % 2) * size_meters,
				    i * size_meters,
				    0.0f,
				};
				c.board.model_f32.emplace_back(p);
				c.board.model_f64.emplace_back(p);
			}
		}
		break;
	}
}

static void
push_model(Calibration &c)
{
	c.state.board_models_f32.push_back(c.board.model_f32);
	c.state.board_models_f64.push_back(c.board.model_f64);
}

static void
push_measurement(ViewState &view)
{
	view.measured_f32.push_back(view.current_f32);
	view.measured_f64.push_back(view.current_f64);
	view.measured_bounds.push_back(view.current_bounds);
}


/*!
 * Returns true if any one of the measurement points have moved.
 */
static bool
has_measurement_moved(MeasurementF64 &last, MeasurementF64 &current)
{
	if (last.size() != current.size()) {
		return true;
	}

	for (size_t i = 0; i < last.size(); ++i) {
		float x = last[i].x - current[i].x;
		float y = last[i].y - current[i].y;

		// Distance squard in pixels.
		if ((x * x + y * y) >= 3.0) {
			return true;
		}
	}

	return false;
}

static bool
moved_state_check(struct ViewState &view)
{
	bool moved = false;
	if (view.last_valid) {
		moved = has_measurement_moved(view.last, view.current_f64);
	}

	// Now save the current measurement to the last one.
	view.last = view.current_f64;
	view.last_valid = true;

	return moved;
}

/*
 *
 * Stereo calibration
 *
 */

#define P(...) snprintf(c.text, sizeof(c.text), __VA_ARGS__)

XRT_NO_INLINE static void
process_stereo_samples(class Calibration &c, int cols, int rows)
{
	c.state.calibrated = true;

	cv::Size image_size(cols, rows);
	cv::Size new_image_size(cols, rows);

	StereoCameraCalibrationWrapper wrapped = {};
	wrapped.view[0].image_size_pixels.w = image_size.width;
	wrapped.view[0].image_size_pixels.h = image_size.height;
	wrapped.view[1].image_size_pixels = wrapped.view[0].image_size_pixels;

	wrapped.view[0].use_fisheye = c.use_fisheye;
	wrapped.view[1].use_fisheye = c.use_fisheye;


	float rp_error = 0.0f;
	if (c.use_fisheye) {
		int flags = 0;
		flags |= cv::fisheye::CALIB_FIX_SKEW;
		flags |= cv::fisheye::CALIB_RECOMPUTE_EXTRINSIC;

		// fisheye version
		rp_error = cv::fisheye::stereoCalibrate(c.state.board_models_f64,               // objectPoints
		                                        c.state.view[0].measured_f64,           // inagePoints1
		                                        c.state.view[1].measured_f64,           // imagePoints2
		                                        wrapped.view[0].intrinsics_mat,         // cameraMatrix1
		                                        wrapped.view[0].distortion_fisheye_mat, // distCoeffs1
		                                        wrapped.view[1].intrinsics_mat,         // cameraMatrix2
		                                        wrapped.view[1].distortion_fisheye_mat, // distCoeffs2
		                                        image_size,                             // imageSize
		                                        wrapped.camera_rotation_mat,            // R
		                                        wrapped.camera_translation_mat,         // T
		                                        flags);
	} else {
		// non-fisheye version
		int flags = 0;

		// Insists on 32-bit floats for object points and image points
		rp_error = cv::stereoCalibrate(c.state.board_models_f32,       // objectPoints
		                               c.state.view[0].measured_f32,   // inagePoints1
		                               c.state.view[1].measured_f32,   // imagePoints2,
		                               wrapped.view[0].intrinsics_mat, // cameraMatrix1
		                               wrapped.view[0].distortion_mat, // distCoeffs1
		                               wrapped.view[1].intrinsics_mat, // cameraMatrix2
		                               wrapped.view[1].distortion_mat, // distCoeffs2
		                               image_size,                     // imageSize
		                               wrapped.camera_rotation_mat,    // R
		                               wrapped.camera_translation_mat, // T
		                               wrapped.camera_essential_mat,   // E
		                               wrapped.camera_fundamental_mat, // F
		                               flags);                         // flags
	}

	// Tell the user what has happened.
	P("CALIBRATION DONE RP ERROR %f", rp_error);

	// Preview undistortion/rectification.
	StereoRectificationMaps maps(wrapped.base);
	c.state.view[0].map1 = maps.view[0].rectify.remap_x;
	c.state.view[0].map2 = maps.view[0].rectify.remap_y;
	c.state.view[0].maps_valid = true;

	c.state.view[1].map1 = maps.view[1].rectify.remap_x;
	c.state.view[1].map2 = maps.view[1].rectify.remap_y;
	c.state.view[1].maps_valid = true;

	// clang-format off
	std::cout << "#####\n";
	std::cout << "calibration rp_error: " << rp_error << "\n";
	to_stdout("camera_rotation", wrapped.camera_rotation_mat);
	to_stdout("camera_translation", wrapped.camera_translation_mat);
	if (!c.use_fisheye) {
		to_stdout("camera_essential", wrapped.camera_essential_mat);
		to_stdout("camera_fundamental", wrapped.camera_fundamental_mat);
	}
	to_stdout("disparity_to_depth", maps.disparity_to_depth_mat);
	std::cout << "#####\n";
	if (c.use_fisheye) {
		to_stdout("view[0].distortion_fisheye", wrapped.view[0].distortion_fisheye_mat);
	} else {
		to_stdout("view[0].distortion", wrapped.view[0].distortion_mat);
	}
	to_stdout("view[0].intrinsics", wrapped.view[0].intrinsics_mat);
	to_stdout("view[0].projection", maps.view[0].projection_mat);
	to_stdout("view[0].rotation", maps.view[0].rotation_mat);
	std::cout << "#####\n";
	if (c.use_fisheye) {
		to_stdout("view[1].distortion_fisheye", wrapped.view[1].distortion_fisheye_mat);
	} else {
		to_stdout("view[1].distortion", wrapped.view[1].distortion_mat);
	}
	to_stdout("view[1].intrinsics", wrapped.view[1].intrinsics_mat);
	to_stdout("view[1].projection", maps.view[1].projection_mat);
	to_stdout("view[1].rotation", maps.view[1].rotation_mat);
	// clang-format on

	// Validate that nothing has been re-allocated.
	assert(wrapped.isDataStorageValid());

	if (c.status != NULL) {
		t_stereo_camera_calibration_reference(&c.status->stereo_data, wrapped.base);
	}
}

static void
process_view_samples(class Calibration &c, struct ViewState &view, int cols, int rows)
{

	const cv::Size image_size = {cols, rows};
	double rp_error = 0.f;

	cv::Mat intrinsics_mat = {};
	cv::Mat new_intrinsics_mat = {};
	cv::Mat distortion_mat = {};
	cv::Mat distortion_fisheye_mat = {};

	if (c.dump_measurements) {
		U_LOG_RAW("...measured = (ArrayOfMeasurements){");
		for (MeasurementF32 &m : view.measured_f32) {
			U_LOG_RAW("  {");
			for (cv::Point2f &p : m) {
				U_LOG_RAW("   {%+ff, %+ff},", p.x, p.y);
			}
			U_LOG_RAW("  },");
		}
		U_LOG_RAW("};");
	}

	if (c.use_fisheye) {
		int crit_flag = 0;
		crit_flag |= cv::TermCriteria::EPS;
		crit_flag |= cv::TermCriteria::COUNT;
		cv::TermCriteria term_criteria = {crit_flag, 100, DBL_EPSILON};

		int flags = 0;
		flags |= cv::fisheye::CALIB_FIX_SKEW;
		flags |= cv::fisheye::CALIB_RECOMPUTE_EXTRINSIC;
#if 0
		flags |= cv::fisheye::CALIB_FIX_PRINCIPAL_POINT;
#endif

		rp_error = cv::fisheye::calibrate(c.state.board_models_f64, // objectPoints
		                                  view.measured_f64,        // imagePoints
		                                  image_size,               // image_size
		                                  intrinsics_mat,           // K (cameraMatrix 3x3)
		                                  distortion_fisheye_mat,   // D (distCoeffs 4x1)
		                                  cv::noArray(),            // rvecs
		                                  cv::noArray(),            // tvecs
		                                  flags,                    // flags
		                                  term_criteria);           // criteria

		double balance = 0.1f;

		cv::fisheye::estimateNewCameraMatrixForUndistortRectify(intrinsics_mat,         // K
		                                                        distortion_fisheye_mat, // D
		                                                        image_size,             // image_size
		                                                        cv::Matx33d::eye(),     // R
		                                                        new_intrinsics_mat,     // P
		                                                        balance);               // balance

		// Probably a busted work-around for busted function.
		new_intrinsics_mat.at<double>(0, 2) = (cols - 1) / 2.0;
		new_intrinsics_mat.at<double>(1, 2) = (rows - 1) / 2.0;
	} else {
		int flags = 0;

		// Go all out.
		flags |= cv::CALIB_THIN_PRISM_MODEL;
		flags |= cv::CALIB_RATIONAL_MODEL;
		flags |= cv::CALIB_TILTED_MODEL;

		rp_error = cv::calibrateCamera( //
		    c.state.board_models_f32,   // objectPoints
		    view.measured_f32,          // imagePoints
		    image_size,                 // imageSize
		    intrinsics_mat,             // cameraMatrix
		    distortion_mat,             // distCoeffs
		    cv::noArray(),              // rvecs
		    cv::noArray(),              // tvecs
		    flags);                     // flags

		// Currently see as much as possible of the original image.
		float alpha = 1.0;

		// Create the new camera matrix.
		new_intrinsics_mat = cv::getOptimalNewCameraMatrix(intrinsics_mat, // cameraMatrix
		                                                   distortion_mat, // distCoeffs
		                                                   image_size,     // imageSize
		                                                   alpha,          // alpha
		                                                   cv::Size(),     // newImgSize
		                                                   NULL,           // validPixROI
		                                                   false);         // centerPrincipalPoint
	}

	P("CALIBRATION DONE RP ERROR %f", rp_error);

	// clang-format off
	std::cout << "image_size: " << image_size << "\n";
	std::cout << "rp_error: " << rp_error << "\n";
	std::cout << "intrinsics_mat:\n" << intrinsics_mat << "\n";
	std::cout << "new_intrinsics_mat:\n" << new_intrinsics_mat << "\n";
	if (c.use_fisheye) {
		std::cout << "distortion_fisheye_mat:\n" << distortion_fisheye_mat << "\n";
	} else {
		std::cout << "distortion_mat:\n" << distortion_mat << "\n";
	}
	// clang-format on

	if (c.use_fisheye) {
		cv::fisheye::initUndistortRectifyMap(intrinsics_mat,         // K
		                                     distortion_fisheye_mat, // D
		                                     cv::Matx33d::eye(),     // R
		                                     new_intrinsics_mat,     // P
		                                     image_size,             // size
		                                     CV_32FC1,               // m1type
		                                     view.map1,              // map1
		                                     view.map2);             // map2

		// Set the maps as valid.
		view.maps_valid = true;
	} else {
		cv::initUndistortRectifyMap( //
		    intrinsics_mat,          // K
		    distortion_mat,          // D
		    cv::noArray(),           // R
		    new_intrinsics_mat,      // P
		    image_size,              // size
		    CV_32FC1,                // m1type
		    view.map1,               // map1
		    view.map2);              // map2

		// Set the maps as valid.
		view.maps_valid = true;
	}

	c.state.calibrated = true;
}

static void
update_public_status(class Calibration &c, bool found)
{
	if (c.status != NULL) {
		int num = (int)c.state.board_models_f32.size();
		c.status->num_collected = num;
		c.status->cooldown = c.state.cooldown;
		c.status->waits_remaining = c.state.waited_for;
		c.status->found = found;
	}
}

/*!
 * Logic for capturing a frame.
 */
static void
do_capture_logic_mono(class Calibration &c, struct ViewState &view, bool found, cv::Mat &gray, cv::Mat &rgb)
{
	int num = (int)c.state.board_models_f32.size();
	int of = c.num_collect_total;
	P("(%i/%i) SHOW BOARD", num, of);
	update_public_status(c, found);

	if (c.state.cooldown > 0) {
		P("(%i/%i) MOVE BOARD TO NEW POSITION", num, of);
		c.state.cooldown--;
		return;
	}

	// We haven't found anything, reset to be beginning.
	if (!found) {
		c.state.waited_for = c.num_wait_for;
		c.state.collected_of_part = 0;
		view.last_valid = false;
		return;
	}

	// We are still waiting for frames.
	if (c.state.waited_for > 0) {
		P("(%i/%i) WAITING %i FRAMES", num, of, c.state.waited_for);
		c.state.waited_for--;

		if (moved_state_check(view)) {
			P("(%i/%i) KEEP BOARD STILL!", num, of);
			c.state.waited_for = c.num_wait_for;
			c.state.collected_of_part = 0;
		}

		return;
	}

	if (c.save_images) {
		char buf[512];

		snprintf(buf, 512, "gray_%ix%i_%03i.png", gray.cols, gray.rows, (int)view.measured_f32.size());
		cv::imwrite(buf, gray);

		snprintf(buf, 512, "debug_rgb_%03i.jpg", (int)view.measured_f32.size());
		cv::imwrite(buf, rgb);
	}

	push_model(c);
	push_measurement(view);

	c.state.collected_of_part++;

	P("(%i/%i) COLLECTED #%i", num, of, c.state.collected_of_part);

	// Have we collected all of the frames for one part?
	if (c.state.collected_of_part >= c.num_collect_restart) {
		c.state.waited_for = c.num_wait_for;
		c.state.collected_of_part = 0;
		c.state.cooldown = c.num_cooldown_frames;
		return;
	}
}

/*!
 * Capture logic for stereo frames.
 */
static void
do_capture_logic_stereo(class Calibration &c,
                        cv::Mat &gray,
                        cv::Mat &rgb,
                        bool l_found,
                        struct ViewState &l_view,
                        cv::Mat &l_gray,
                        cv::Mat &l_rgb,
                        bool r_found,
                        struct ViewState &r_view,
                        cv::Mat &r_gray,
                        cv::Mat &r_rgb)
{
	bool found = l_found && r_found;

	int num = (int)c.state.board_models_f32.size();
	int of = c.num_collect_total;
	P("(%i/%i) SHOW BOARD %i %i", num, of, l_found, r_found);
	update_public_status(c, found);

	if (c.state.cooldown > 0) {
		P("(%i/%i) MOVE BOARD TO NEW POSITION", num, of);
		c.state.cooldown--;
		return;
	}

	// We haven't found anything, reset to be beginning.
	if (!found) {
		c.state.waited_for = c.num_wait_for;
		c.state.collected_of_part = 0;
		l_view.last_valid = false;
		r_view.last_valid = false;
		return;
	}

	// We are still waiting for frames.
	if (c.state.waited_for > 0) {
		P("(%i/%i) WAITING %i FRAMES", num, of, c.state.waited_for);
		c.state.waited_for--;

		bool l_moved = moved_state_check(l_view);
		bool r_moved = moved_state_check(r_view);
		bool moved = l_moved || r_moved;

		if (moved) {
			P("(%i/%i) KEEP BOARD STILL!", num, of);
			c.state.waited_for = c.num_wait_for;
			c.state.collected_of_part = 0;
		}

		return;
	}

	if (c.save_images) {
		char buf[512];

		snprintf(buf, 512, "gray_%ix%i_%03i.png", gray.cols, gray.rows, (int)c.state.board_models_f32.size());
		cv::imwrite(buf, gray);

		snprintf(buf, 512, "debug_rgb_%03i.jpg", (int)c.state.board_models_f32.size());
		cv::imwrite(buf, rgb);
	}

	push_model(c);
	push_measurement(c.state.view[0]);
	push_measurement(c.state.view[1]);

	c.state.collected_of_part++;

	P("(%i/%i) COLLECTED #%i", num, of, c.state.collected_of_part);

	// Have we collected all of the frames for one part?
	if (c.state.collected_of_part >= c.num_collect_restart) {
		c.state.waited_for = c.num_wait_for;
		c.state.collected_of_part = 0;
		c.state.cooldown = c.num_cooldown_frames;
		return;
	}
}

/*!
 * Make a mono frame.
 */
static void
make_calibration_frame_mono(class Calibration &c)
{
	auto &rgb = c.gui.rgb;
	auto &gray = c.gray;

	bool found = do_view(c, c.state.view[0], gray, rgb);

	// Advance the state of the calibration.
	do_capture_logic_mono(c, c.state.view[0], found, gray, rgb);

	if (c.state.board_models_f32.size() >= c.num_collect_total) {
		process_view_samples(c, c.state.view[0], rgb.cols, rgb.rows);
	}

	// Draw text and finally send the frame off.
	print_txt(rgb, c.text, 1.5);
	send_rgb_frame(c);
}

/*!
 * Make a stereo frame side by side.
 */
static void
make_calibration_frame_sbs(class Calibration &c)
{
	auto &rgb = c.gui.rgb;
	auto &gray = c.gray;

	int cols = rgb.cols / 2;
	int rows = rgb.rows;

	// Split left and right eyes, don't make any copies.
	cv::Mat l_gray(rows, cols, CV_8UC1, gray.data, gray.cols);
	cv::Mat r_gray(rows, cols, CV_8UC1, gray.data + cols, gray.cols);
	cv::Mat l_rgb(rows, cols, CV_8UC3, c.gui.frame->data, c.gui.frame->stride);
	cv::Mat r_rgb(rows, cols, CV_8UC3, c.gui.frame->data + 3 * cols, c.gui.frame->stride);

	bool found_left = do_view(c, c.state.view[0], l_gray, l_rgb);
	bool found_right = do_view(c, c.state.view[1], r_gray, r_rgb);

	do_capture_logic_stereo(c, gray, rgb, found_left, c.state.view[0], l_gray, l_rgb, found_right, c.state.view[1],
	                        r_gray, r_rgb);

	if (c.state.board_models_f32.size() >= c.num_collect_total) {
		process_stereo_samples(c, cols, rows);
	}

	// Draw text and finally send the frame off.
	print_txt(rgb, c.text, 1.5);
	send_rgb_frame(c);
}

static void
make_calibration_frame(class Calibration &c, struct xrt_frame *xf)
{
	switch (xf->stereo_format) {
	case XRT_STEREO_FORMAT_SBS: make_calibration_frame_sbs(c); break;
	case XRT_STEREO_FORMAT_NONE: make_calibration_frame_mono(c); break;
	default:
		P("ERROR: Unknown stereo format! '%i'", xf->stereo_format);
		make_gui_str(c);
		return;
	}

	if (c.status != NULL && c.state.calibrated) {
		c.status->finished = true;
	}
}

static void
make_remap_view(class Calibration &c, struct xrt_frame *xf)
{
	cv::Mat &rgb = c.gui.rgb;
	struct xrt_frame &frame = *c.gui.frame;

	switch (xf->stereo_format) {
	case XRT_STEREO_FORMAT_SBS: {
		int cols = rgb.cols / 2;
		int rows = rgb.rows;

		cv::Mat l_rgb(rows, cols, CV_8UC3, frame.data, frame.stride);
		cv::Mat r_rgb(rows, cols, CV_8UC3, frame.data + 3 * cols, frame.stride);

		remap_view(c, c.state.view[0], l_rgb);
		remap_view(c, c.state.view[1], r_rgb);
	} break;
	case XRT_STEREO_FORMAT_NONE: {
		remap_view(c, c.state.view[0], rgb);
	} break;
	default:
		P("ERROR: Unknown stereo format! '%i'", xf->stereo_format);
		make_gui_str(c);
		return;
	}
}


/*
 *
 * Main functions.
 *
 */

XRT_NO_INLINE static void
process_frame_l8(class Calibration &c, struct xrt_frame *xf)
{

	int w = (int)xf->width;
	int h = (int)xf->height;

	cv::Mat data(h, w, CV_8UC1, xf->data, xf->stride);
	c.gray = data;
	ensure_buffers_are_allocated(c, data.rows, data.cols);
	c.gui.frame->source_sequence = xf->source_sequence;

	cv::cvtColor(data, c.gui.rgb, cv::COLOR_GRAY2RGB);
}

XRT_NO_INLINE static void
process_frame_yuv(class Calibration &c, struct xrt_frame *xf)
{

	int w = (int)xf->width;
	int h = (int)xf->height;

	cv::Mat data(h, w, CV_8UC3, xf->data, xf->stride);
	ensure_buffers_are_allocated(c, data.rows, data.cols);
	c.gui.frame->source_sequence = xf->source_sequence;

	cv::cvtColor(data, c.gui.rgb, cv::COLOR_YUV2RGB);
	cv::cvtColor(c.gui.rgb, c.gray, cv::COLOR_RGB2GRAY);
}

XRT_NO_INLINE static void
process_frame_yuyv(class Calibration &c, struct xrt_frame *xf)
{
	/*
	 * Cleverly extract the different channels.
	 * Cr/Cb are extracted at half width.
	 */
	int w = (int)xf->width;
	int h = (int)xf->height;

	cv::Mat data_full(h, w, CV_8UC2, xf->data, xf->stride);
	ensure_buffers_are_allocated(c, data_full.rows, data_full.cols);
	c.gui.frame->source_sequence = xf->source_sequence;

	cv::cvtColor(data_full, c.gui.rgb, cv::COLOR_YUV2RGB_YUYV);
	cv::cvtColor(data_full, c.gray, cv::COLOR_YUV2GRAY_YUYV);
}

XRT_NO_INLINE static void
process_frame_uyvy(class Calibration &c, struct xrt_frame *xf)
{
	/*
	 * Cleverly extract the different channels.
	 * Cr/Cb are extracted at half width.
	 */
	int w = (int)xf->width;
	int h = (int)xf->height;

	cv::Mat data_full(h, w, CV_8UC2, xf->data, xf->stride);
	ensure_buffers_are_allocated(c, data_full.rows, data_full.cols);
	c.gui.frame->source_sequence = xf->source_sequence;

	cv::cvtColor(data_full, c.gui.rgb, cv::COLOR_YUV2RGB_UYVY);
	cv::cvtColor(data_full, c.gray, cv::COLOR_YUV2GRAY_UYVY);
}

XRT_NO_INLINE static void
process_load_image(class Calibration &c, struct xrt_frame *xf)
{
	char buf[512];

	// We need to change the settings for frames to make it work.
	uint32_t num_collect_restart = 1;
	uint32_t num_cooldown_frames = 0;
	uint32_t num_wait_for = 0;

	std::swap(c.num_collect_restart, num_collect_restart);
	std::swap(c.num_cooldown_frames, num_cooldown_frames);
	std::swap(c.num_wait_for, num_wait_for);

	for (uint32_t i = 0; i < c.load.num_images; i++) {
		// Early out if the user requeted less images.
		if (c.state.calibrated) {
			break;
		}

		snprintf(buf, 512, "gray_%ux%u_%03i.png", xf->width, xf->height, i);
		c.gray = cv::imread(buf, cv::IMREAD_GRAYSCALE);

		if (c.gray.rows == 0 || c.gray.cols == 0) {
			U_LOG_E("Could not find image '%s'!", buf);
			continue;
		}

		if (c.gray.rows != (int)xf->height || c.gray.cols != (int)xf->width) {
			U_LOG_E(
			    "Image size does not match frame size! Image: "
			    "(%ix%i) Frame: (%ux%u)",
			    c.gray.cols, c.gray.rows, xf->width, xf->height);
			continue;
		}

		// Create a new RGB image and then copy the gray data to it.
		refresh_gui_frame(c, c.gray.rows, c.gray.cols);
		cv::cvtColor(c.gray, c.gui.rgb, cv::COLOR_GRAY2RGB);

		if (c.stereo_sbs) {
			xf->stereo_format = XRT_STEREO_FORMAT_SBS;
		}

		// Call the normal frame processing now.
		make_calibration_frame(c, xf);
	}

	// Restore settings.
	c.num_collect_restart = num_collect_restart;
	c.num_cooldown_frames = num_cooldown_frames;
	c.num_wait_for = num_wait_for;

	c.load.enabled = false;
}


/*
 *
 * Interface functions.
 *
 */

extern "C" void
t_calibration_frame(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	auto &c = *(class Calibration *)xsink;

	if (c.load.enabled) {
		process_load_image(c, xf);
	}

	// Fill both c.gui.rgb and c.gray with the data we got.
	switch (xf->format) {
	case XRT_FORMAT_YUV888: process_frame_yuv(c, xf); break;
	case XRT_FORMAT_YUYV422: process_frame_yuyv(c, xf); break;
	case XRT_FORMAT_UYVY422: process_frame_uyvy(c, xf); break;
	case XRT_FORMAT_L8: process_frame_l8(c, xf); break;
	default:
		P("ERROR: Bad format '%s'", u_format_str(xf->format));
		make_gui_str(c);
		return;
	}

	// Don't do anything if we are done.
	if (c.state.calibrated) {
		make_remap_view(c, xf);

		print_txt(c.gui.rgb, c.text, 1.5);

		send_rgb_frame(c);
		return;
	}

	// Clear our gui frame.
	if (c.clear_frame) {
		cv::rectangle(c.gui.rgb, cv::Point2f(0, 0), cv::Point2f(c.gui.rgb.cols, c.gui.rgb.rows),
		              cv::Scalar(0, 0, 0), -1, 0);
	}

	make_calibration_frame(c, xf);
}


/*
 *
 * Exported functions.
 *
 */

extern "C" int
t_calibration_stereo_create(struct xrt_frame_context *xfctx,
                            const struct t_calibration_params *params,
                            struct t_calibration_status *status,
                            struct xrt_frame_sink *gui,
                            struct xrt_frame_sink **out_sink)
{
	auto &c = *(new Calibration());

	// Basic setup.
	c.gui.sink = gui;
	c.base.push_frame = t_calibration_frame;
	*out_sink = &c.base;

	// Copy the parameters.
	c.use_fisheye = params->use_fisheye;
	c.stereo_sbs = params->stereo_sbs;
	c.board.pattern = params->pattern;
	switch (params->pattern) {
	case T_BOARD_CHECKERS:
		c.board.dims = {
		    params->checkers.cols - 1,
		    params->checkers.rows - 1,
		};
		c.board.spacing_meters = params->checkers.size_meters;
		c.subpixel_enable = params->checkers.subpixel_enable;
		c.subpixel_size = params->checkers.subpixel_size;
		break;
	case T_BOARD_CIRCLES:
		c.board.dims = {
		    params->circles.cols,
		    params->circles.rows,
		};
		c.board.spacing_meters = params->circles.distance_meters;
		break;
	case T_BOARD_ASYMMETRIC_CIRCLES:
		c.board.dims = {
		    params->asymmetric_circles.cols,
		    params->asymmetric_circles.rows,
		};
		c.board.spacing_meters = params->asymmetric_circles.diagonal_distance_meters;
		break;
	default: assert(false);
	}
	c.num_cooldown_frames = params->num_cooldown_frames;
	c.num_wait_for = params->num_wait_for;
	c.num_collect_total = params->num_collect_total;
	c.num_collect_restart = params->num_collect_restart;
	c.load.enabled = params->load.enabled;
	c.load.num_images = params->load.num_images;
	c.mirror_rgb_image = params->mirror_rgb_image;
	c.save_images = params->save_images;
	c.status = status;


	// Setup a initial message.
	P("Waiting for camera");
	make_gui_str(c);

	int ret = 0;
	if (debug_get_bool_option_hsv_filter()) {
		ret = t_debug_hsv_filter_create(xfctx, *out_sink, out_sink);
	}

	if (debug_get_bool_option_hsv_picker()) {
		ret = t_debug_hsv_picker_create(xfctx, *out_sink, out_sink);
	}

	if (debug_get_bool_option_hsv_viewer()) {
		ret = t_debug_hsv_viewer_create(xfctx, *out_sink, out_sink);
	}

	// Ensure we only get yuv, yuyv, uyvy or l8 frames.
	u_sink_create_to_yuv_yuyv_uyvy_or_l8(xfctx, *out_sink, out_sink);


	// Build the board model.
	build_board_position(c);

	// Pre allocate
	c.state.view[0].current_f32.reserve(c.board.model_f32.size());
	c.state.view[0].current_f64.reserve(c.board.model_f64.size());
	c.state.view[1].current_f32.reserve(c.board.model_f32.size());
	c.state.view[1].current_f64.reserve(c.board.model_f64.size());

#if 0
	c.state.view[0].measured = (ArrayOfMeasurements){
	};

	c.state.view[1].measured = (ArrayOfMeasurements){
	};

	for (Measurement &m : c.state.view[0].measured) {
		(void)m;
		push_model(c);
	}
#endif
	return ret;
}

//! Helper for NormalizedCoordsCache constructors
static inline std::vector<cv::Vec2f>
generateInputCoordsAndReserveOutputCoords(cv::Size size, std::vector<cv::Vec2f> &outputCoords)
{
	std::vector<cv::Vec2f> inputCoords;

	const auto n = size.width * size.height;
	assert(n != 0);
	inputCoords.reserve(n);
	for (int row = 0; row < size.height; ++row) {
		for (int col = 0; col < size.width; ++col) {
			inputCoords.emplace_back(col, row);
		}
	}
	outputCoords.reserve(inputCoords.size());
	return inputCoords;
}

//! Helper for NormalizedCoordsCache constructors
static inline void
populateCacheMats(cv::Size size,
                  const std::vector<cv::Vec2f> &inputCoords,
                  const std::vector<cv::Vec2f> &outputCoords,
                  cv::Mat_<float> &cacheX,
                  cv::Mat_<float> &cacheY)
{
	assert(size.height != 0);
	assert(size.width != 0);
	cacheX.create(size);
	cacheY.create(size);
	const auto n = size.width * size.height;
	// Populate the cache matrices
	for (int i = 0; i < n; ++i) {
		auto input = cv::Point{int(inputCoords[i][0]), int(inputCoords[i][1])};
		cacheX(input) = outputCoords[i][0];
		cacheY(input) = outputCoords[i][1];
	}
}

NormalizedCoordsCache::NormalizedCoordsCache(cv::Size size, // NOLINT // small, pass by value
                                             const cv::Matx33d &intrinsics,
                                             const cv::Matx<double, 5, 1> &distortion)
{
	std::vector<cv::Vec2f> outputCoords;
	std::vector<cv::Vec2f> inputCoords = generateInputCoordsAndReserveOutputCoords(size, outputCoords);
	// Undistort/reproject those coordinates in one call, to make use of
	// cached internal/intermediate computations.
	cv::undistortPoints(inputCoords, outputCoords, intrinsics, distortion);

	populateCacheMats(size, inputCoords, outputCoords, cacheX_, cacheY_);
}
NormalizedCoordsCache::NormalizedCoordsCache(cv::Size size, // NOLINT // small, pass by value
                                             const cv::Matx33d &intrinsics,
                                             const cv::Matx<double, 5, 1> &distortion,
                                             const cv::Matx33d &rectification,
                                             const cv::Matx33d &new_camera_matrix)
{
	std::vector<cv::Vec2f> outputCoords;
	std::vector<cv::Vec2f> inputCoords = generateInputCoordsAndReserveOutputCoords(size, outputCoords);
	// Undistort/reproject those coordinates in one call, to make use of
	// cached internal/intermediate computations.
	cv::undistortPoints(inputCoords, outputCoords, intrinsics, distortion, rectification, new_camera_matrix);

	populateCacheMats(size, inputCoords, outputCoords, cacheX_, cacheY_);
}

NormalizedCoordsCache::NormalizedCoordsCache(cv::Size size, // NOLINT // small, pass by value
                                             const cv::Matx33d &intrinsics,
                                             const cv::Matx<double, 5, 1> &distortion,
                                             const cv::Matx33d &rectification,
                                             const cv::Matx<double, 3, 4> &new_projection_matrix)
{
	std::vector<cv::Vec2f> outputCoords;
	std::vector<cv::Vec2f> inputCoords = generateInputCoordsAndReserveOutputCoords(size, outputCoords);
	// Undistort/reproject those coordinates in one call, to make use of
	// cached internal/intermediate computations.
	cv::undistortPoints(inputCoords, outputCoords, intrinsics, distortion, rectification, new_projection_matrix);

	populateCacheMats(size, inputCoords, outputCoords, cacheX_, cacheY_);
}
NormalizedCoordsCache::NormalizedCoordsCache(cv::Size size, // NOLINT // small, pass by value
                                             const cv::Mat &intrinsics,
                                             const cv::Mat &distortion)
{
	std::vector<cv::Vec2f> outputCoords;
	std::vector<cv::Vec2f> inputCoords = generateInputCoordsAndReserveOutputCoords(size, outputCoords);
	// Undistort/reproject those coordinates in one call, to make use of
	// cached internal/intermediate computations.
	cv::undistortPoints(inputCoords, outputCoords, intrinsics, distortion);

	populateCacheMats(size, inputCoords, outputCoords, cacheX_, cacheY_);
}

cv::Vec2f
NormalizedCoordsCache::getNormalizedImageCoords(
    // NOLINTNEXTLINE // small, pass by value
    cv::Point2f origCoords) const
{
	/*
	 * getRectSubPix is more strict than the docs would imply:
	 *
	 * - Source must be 1 or 3 channels
	 * - Can sample from u8 into u8, u8 into f32, or f32 into f32 - that's
	 *   it (though the latter is provided by a template function internally
	 *   so could be extended...)
	 */
	cv::Mat patch;
	cv::getRectSubPix(cacheX_, cv::Size(1, 1), origCoords, patch);
	auto x = patch.at<float>(0, 0);
	cv::getRectSubPix(cacheY_, cv::Size(1, 1), origCoords, patch);
	auto y = patch.at<float>(0, 0);
	return {x, y};
}

cv::Vec3f
NormalizedCoordsCache::getNormalizedVector(cv::Point2f origCoords) const
{
	// cameras traditionally look along -z, so we want negative sqrt
	auto pt = getNormalizedImageCoords(std::move(origCoords));
	auto z = -std::sqrt(1.f - pt.dot(pt));
	return {pt[0], pt[1], z};
}
