// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Calibration code.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
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


DEBUG_GET_ONCE_BOOL_OPTION(hsv_filter, "T_DEBUG_HSV_FILTER", false)
DEBUG_GET_ONCE_BOOL_OPTION(hsv_picker, "T_DEBUG_HSV_PICKER", false)
DEBUG_GET_ONCE_BOOL_OPTION(hsv_viewer, "T_DEBUG_HSV_VIEWER", false)


/*
 *
 * Structs
 *
 */

//! A point in the @ref Model.
typedef cv::Point3d ModelPoint;
//! A point in the @ref Measurement.
typedef cv::Point2d MeasurementPoint;
//! Model of the thing we are measuring to calibrate.
typedef std::vector<ModelPoint> Model;
//! A measurement of the model as viewed on the camera.
typedef std::vector<MeasurementPoint> Measurement;
//! In floats, because OpenCV can't agree on a single type to use.
typedef std::vector<cv::Point2f> MeasurementDisplay;
//! For each @ref Measurement we take we also save the @ref Model.
typedef std::vector<Model> ArrayOfModels;
//! A array of @ref Measurement.
typedef std::vector<Measurement> ArrayOfMeasurements;
//! A array of bounding rects.
typedef std::vector<cv::Rect> ArrayOfRects;

/*!
 * Current state for each view, one view for mono cameras, two for stereo.
 */
struct ViewState
{
	ArrayOfMeasurements measured = {};
	ArrayOfRects measuredBounds = {};

	bool last_valid = false;
	Measurement last = {};

	Measurement current = {};
	MeasurementDisplay currentDisplay = {};
	cv::Rect currentBounds = {};

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
		Model model = {};
		cv::Size dims = {8, 6};
		enum t_board_pattern pattern = T_BOARD_CHECKERS;
		float spacing_meters = 0.05;
	} board;

	struct
	{
		ViewState view[2] = {};

		ArrayOfModels board_models = {};

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
refresh_gui_frame(class Calibration &c, int rows, int cols)
{
	// Also dereferences the old frame.
	u_frame_create_one_off(XRT_FORMAT_R8G8B8, cols, rows, &c.gui.frame);

	c.gui.rgb = cv::Mat(rows, cols, CV_8UC3, c.gui.frame->data,
	                    c.gui.frame->stride);
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

	c.gray = cv::Mat(rows, cols, CV_8UC1, cv::Scalar(0));

	refresh_gui_frame(c, rows, cols);
}

static void
print_txt(cv::Mat &rgb, const char *text, double fontScale)
{
	int fontFace = 0;
	int thickness = 2;
	cv::Size textSize =
	    cv::getTextSize(text, fontFace, fontScale, thickness, NULL);

	cv::Point textOrg((rgb.cols - textSize.width) / 2, textSize.height * 2);

	cv::putText(rgb, text, textOrg, fontFace, fontScale,
	            cv::Scalar(192, 192, 192), thickness);
}

static void
make_gui_str(class Calibration &c)
{
	auto &rgb = c.gui.rgb;

	int cols = 800;
	int rows = 100;
	ensure_buffers_are_allocated(c, rows, cols);

	cv::rectangle(rgb, cv::Point2f(0, 0), cv::Point2f(cols, rows),
	              cv::Scalar(0, 0, 0), -1, 0);

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
do_view_coverage(class Calibration &c,
                 struct ViewState &view,
                 cv::Mat &gray,
                 cv::Mat &rgb,
                 bool found)
{
	// Clear the display and convert from the measurement.
	view.currentDisplay.clear();
	for (const MeasurementPoint &p : view.current) {
		view.currentDisplay.push_back(cv::Point2f(p.x, p.y));
	}

	// Get the current bounding rect.
	view.currentBounds = cv::boundingRect(view.currentDisplay);

	// Compute our 'pre sample' coverage for this frame,
	// for display and area threshold checking.
	std::vector<cv::Point2f> coverage;
	for (uint32_t i = 0; i < view.measured.size(); i++) {
		cv::Rect brect = view.measuredBounds[i];

		draw_rect(rgb, brect, cv::Scalar(0, 64, 32));

		coverage.push_back(cv::Point2f(brect.tl()));
		coverage.push_back(cv::Point2f(brect.br()));
	}

	// What area of the camera have we calibrated.
	view.pre_rect = cv::boundingRect(coverage);
	draw_rect(rgb, view.pre_rect, cv::Scalar(0, 255, 255));

	if (found) {
		coverage.push_back(cv::Point2f(view.currentBounds.tl()));
		coverage.push_back(cv::Point2f(view.currentBounds.br()));

		// New area we cover.
		view.post_rect = cv::boundingRect(coverage);

		draw_rect(rgb, view.post_rect, cv::Scalar(0, 255, 0));
	}

	// Draw the checker board, will also draw partial hits.
	cv::drawChessboardCorners(rgb, c.board.dims, view.currentDisplay,
	                          found);
}

static bool
do_view_chess(class Calibration &c,
              struct ViewState &view,
              cv::Mat &gray,
              cv::Mat &rgb)
{
	int flags = 0;
	flags += cv::CALIB_CB_FAST_CHECK;
	flags += cv::CALIB_CB_ADAPTIVE_THRESH;
	flags += cv::CALIB_CB_NORMALIZE_IMAGE;

	bool found = cv::findChessboardCorners(gray,         // Image
	                                       c.board.dims, // patternSize
	                                       view.current, // corners
	                                       flags);       // flags

	// Improve the corner positions.
	if (found && c.subpixel_enable) {
		int crit_flag = 0;
		crit_flag |= cv::TermCriteria::EPS;
		crit_flag |= cv::TermCriteria::COUNT;
		cv::TermCriteria term_criteria = {crit_flag, 30, 0.1};

		cv::Size size(c.subpixel_size, c.subpixel_size);
		cv::Size zero(-1, -1);

		cv::cornerSubPix(gray, view.current, size, zero, term_criteria);
	}

	do_view_coverage(c, view, gray, rgb, found);

	return found;
}

static bool
do_view_circles(class Calibration &c,
                struct ViewState &view,
                cv::Mat &gray,
                cv::Mat &rgb)
{
	int flags = 0;
	if (c.board.pattern == T_BOARD_ASYMMETRIC_CIRCLES) {
		flags |= cv::CALIB_CB_ASYMMETRIC_GRID;
	}

	bool found = cv::findCirclesGrid(gray,         // Image
	                                 c.board.dims, // patternSize
	                                 view.current, // corners
	                                 flags);       // flags

	do_view_coverage(c, view, gray, rgb, found);

	return found;
}

static bool
do_view(class Calibration &c,
        struct ViewState &view,
        cv::Mat &gray,
        cv::Mat &rgb)
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
		for (int i = 0; i < rows_num; ++i) {
			for (int j = 0; j < cols_num; ++j) {
				cv::Point3f p = {
				    j * size_meters,
				    i * size_meters,
				    0.0f,
				};
				c.board.model.push_back(p);
			}
		}
		break;
	case T_BOARD_ASYMMETRIC_CIRCLES:
		for (int i = 0; i < rows_num; ++i) {
			for (int j = 0; j < cols_num; ++j) {
				cv::Point3f p = {
				    (2 * j + i % 2) * size_meters,
				    i * size_meters,
				    0.0f,
				};
				c.board.model.push_back(p);
			}
		}
		break;
	}
}

/*!
 * Returns true if any one of the measurement points have moved.
 */
static bool
has_measurement_moved(Measurement &last, Measurement &current)
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
		moved = has_measurement_moved(view.last, view.current);
	}

	// Now save the current measurement to the last one.
	view.last = view.current;
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

	CalibrationRawData raw = {};
	assert(raw.isDataStorageValid());

	raw.use_fisheye = c.use_fisheye;
	raw.image_size_pixels.w = image_size.width;
	raw.image_size_pixels.h = image_size.height;
	raw.new_image_size_pixels.w = new_image_size.width;
	raw.new_image_size_pixels.h = new_image_size.height;


	float rp_error = 0.0f;
	if (c.use_fisheye) {
		int flags = 0;
		flags |= cv::fisheye::CALIB_FIX_SKEW;
		flags |= cv::fisheye::CALIB_RECOMPUTE_EXTRINSIC;

		// fisheye version
		rp_error = cv::fisheye::stereoCalibrate(
		    c.state.board_models,         // objectPoints
		    c.state.view[0].measured,     // inagePoints1
		    c.state.view[1].measured,     // imagePoints2
		    raw.l_intrinsics_mat,         // cameraMatrix1
		    raw.l_distortion_fisheye_mat, // distCoeffs1
		    raw.r_intrinsics_mat,         // cameraMatrix2
		    raw.r_distortion_fisheye_mat, // distCoeffs2
		    image_size,                   // imageSize
		    raw.camera_rotation_mat,      // R
		    raw.camera_translation_mat,   // T
		    flags);
	} else {
		// non-fisheye version
		rp_error = cv::stereoCalibrate(
		    c.state.board_models,       // objectPoints
		    c.state.view[0].measured,   // inagePoints1
		    c.state.view[1].measured,   // imagePoints2,
		    raw.l_intrinsics_mat,       // cameraMatrix1
		    raw.l_distortion_mat,       // distCoeffs1
		    raw.r_intrinsics_mat,       // cameraMatrix2
		    raw.r_distortion_mat,       // distCoeffs2
		    image_size,                 // imageSize
		    raw.camera_rotation_mat,    // R
		    raw.camera_translation_mat, // T
		    raw.camera_essential_mat,   // E
		    raw.camera_fundamental_mat, // F
		    0);                         // flags
	}

	assert(raw.camera_rotation_mat.size() == cv::Size(3, 3));
	assert(raw.camera_translation_mat.size() == cv::Size(1, 3));
	assert(raw.camera_essential_mat.size() == cv::Size(3, 3));
	assert(raw.camera_fundamental_mat.size() == cv::Size(3, 3));

#if 0
	// stereoRectify just yields very bad results. :(
	if (c.use_fisheye) {
		cv::fisheye::stereoRectify(
		    raw.l_intrinsics_mat,         // cameraMatrix1
		    raw.r_distortion_fisheye_mat, // distCoeffs1
		    raw.r_intrinsics_mat,         // cameraMatrix2
		    raw.r_distortion_fisheye_mat, // distCoeffs2
		    image_size,                   // imageSize
		    raw.camera_rotation_mat,      // R
		    raw.camera_translation_mat,   // T
		    raw.l_rotation_mat,           // R1
		    raw.r_rotation_mat,           // R2
		    raw.l_projection_mat,         // P1
		    raw.r_projection_mat,         // P2
		    raw.disparity_to_depth_mat,   // Q
		    cv::CALIB_ZERO_DISPARITY,     // flags
		    new_image_size,               // newImageSize
		    0.0,                          // balance
		    1.0);                         // fov_scale
	} else
#endif
	{
		// We currently don't change the image size or remove invalid
		// pixels.
		cv::stereoRectify(raw.l_intrinsics_mat,       // cameraMatrix1
		                  cv::noArray(),              // distCoeffs1
		                  raw.r_intrinsics_mat,       // cameraMatrix2
		                  cv::noArray(),              // distCoeffs2
		                  image_size,                 // imageSize
		                  raw.camera_rotation_mat,    // R
		                  raw.camera_translation_mat, // T
		                  raw.l_rotation_mat,         // R1
		                  raw.r_rotation_mat,         // R2
		                  raw.l_projection_mat,       // P1
		                  raw.r_projection_mat,       // P2
		                  raw.disparity_to_depth_mat, // Q
		                  cv::CALIB_ZERO_DISPARITY,   // flags
		                  -1,                         // alpha
		                  new_image_size,             // newImageSize
		                  NULL,                       // validPixROI1
		                  NULL);                      // validPixROI2
	}

	// Validate that nothing has been re-allocated.
	assert(raw.isDataStorageValid());

	P("CALIBRATION DONE RP ERROR %f", rp_error);

	// clang-format off
	std::cout << "#####\n";
	std::cout << "calibration rp_error: " << rp_error << "\n";
	std::cout << "camera_rotation:\n" << raw.camera_rotation_mat << "\n";
	std::cout << "camera_translation:\n" << raw.camera_translation_mat << "\n";
	if (!c.use_fisheye) {
		std::cout << "camera_essential:\n" << raw.camera_essential_mat << "\n";
		std::cout << "camera_fundamental:\n" << raw.camera_fundamental_mat << "\n";
	}
	std::cout << "#####\n";
	if (c.use_fisheye) {
		std::cout << "l_distortion_fisheye_mat:\n" << raw.l_distortion_fisheye_mat << "\n";
	} else {
		std::cout << "l_distortion_mat:\n" << raw.l_distortion_mat << "\n";
	}
	std::cout << "l_intrinsics_mat:\n" << raw.l_intrinsics_mat << "\n";
	std::cout << "l_projection_mat:\n" << raw.l_projection_mat << "\n";
	std::cout << "l_rotation_mat:\n" << raw.l_rotation_mat << "\n";
	std::cout << "#####\n";
	if (c.use_fisheye) {
		std::cout << "r_distortion_fisheye_mat:\n" << raw.r_distortion_fisheye_mat << "\n";
	} else {
		std::cout << "r_distortion_mat:\n" << raw.r_distortion_mat << "\n";
	}
	std::cout << "r_intrinsics_mat:\n" << raw.r_intrinsics_mat << "\n";
	std::cout << "r_projection_mat:\n" << raw.r_projection_mat << "\n";
	std::cout << "r_rotation_mat:\n" << raw.r_rotation_mat << "\n";
	// clang-format on

	t_file_save_raw_data_hack(&raw);

	// Preview undistortion.
	if (c.use_fisheye) {
		cv::fisheye::initUndistortRectifyMap(
		    raw.l_intrinsics_mat,         // cameraMatrix
		    raw.l_distortion_fisheye_mat, // distCoeffs
		    raw.l_rotation_mat,           // R
		    raw.l_projection_mat,         // newCameraMatrix
		    image_size,                   // size
		    CV_32FC1,                     // m1type
		    c.state.view[0].map1,         // map1
		    c.state.view[0].map2);        // map2

		cv::fisheye::initUndistortRectifyMap(
		    raw.r_intrinsics_mat,         // cameraMatrix
		    raw.r_distortion_fisheye_mat, // distCoeffs
		    raw.r_rotation_mat,           // R
		    raw.r_projection_mat,         // newCameraMatrix
		    image_size,                   // size
		    CV_32FC1,                     // m1type
		    c.state.view[1].map1,         // map1
		    c.state.view[1].map2);        // map2
	} else {
		cv::initUndistortRectifyMap(
		    raw.l_intrinsics_mat,  // cameraMatrix
		    raw.l_distortion_mat,  // distCoeffs
		    cv::Matx33d::eye(),    // R
		    raw.l_intrinsics_mat,  // newCameraMatrix
		    image_size,            // size
		    CV_32FC1,              // m1type
		    c.state.view[0].map1,  // map1
		    c.state.view[0].map2); // map2

		cv::initUndistortRectifyMap(
		    raw.r_intrinsics_mat,  // cameraMatrix
		    raw.l_distortion_mat,  // distCoeffs
		    cv::Matx33d::eye(),    // R
		    raw.l_intrinsics_mat,  // newCameraMatrix
		    image_size,            // size
		    CV_32FC1,              // m1type
		    c.state.view[1].map1,  // map1
		    c.state.view[1].map2); // map2
	}

	// Set the maps as valid.
	c.state.view[0].maps_valid = true;
	c.state.view[1].maps_valid = true;
}

static void
process_view_samples(class Calibration &c,
                     struct ViewState &view,
                     int cols,
                     int rows)
{

	const cv::Size image_size = {cols, rows};
	double rp_error = 0.f;

	cv::Mat intrinsics_mat = {};
	cv::Mat new_intrinsics_mat = {};
	cv::Mat distortion_mat = {};
	cv::Mat distortion_fisheye_mat = {};

	if (c.dump_measurements) {
		printf("...measured = (ArrayOfMeasurements){\n");
		for (Measurement &m : view.measured) {
			printf("  {\n");
			for (MeasurementPoint &p : m) {
				printf("   {%+ff, %+ff},\n", p.x, p.y);
			}
			printf("  },\n");
		}
		printf("};\n");
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

		rp_error = cv::fisheye::calibrate(
		    c.state.board_models,   // objectPoints
		    view.measured,          // imagePoints
		    image_size,             // image_size
		    intrinsics_mat,         // K (cameraMatrix 3x3)
		    distortion_fisheye_mat, // D (distCoeffs 4x1)
		    cv::noArray(),          // rvecs
		    cv::noArray(),          // tvecs
		    flags,                  // flags
		    term_criteria);         // criteria

		double balance = 0.1f;

		cv::fisheye::estimateNewCameraMatrixForUndistortRectify(
		    intrinsics_mat,         // K
		    distortion_fisheye_mat, // D
		    image_size,             // image_size
		    cv::Matx33d::eye(),     // R
		    new_intrinsics_mat,     // P
		    balance);               // balance

		// Probably a busted work-around for busted function.
		new_intrinsics_mat.at<double>(0, 2) = (cols - 1) / 2.0;
		new_intrinsics_mat.at<double>(1, 2) = (rows - 1) / 2.0;
	} else {
		rp_error = cv::calibrateCamera( //
		    c.state.board_models,       // objectPoints
		    view.measured,              // imagePoints
		    image_size,                 // imageSize
		    intrinsics_mat,             // cameraMatrix
		    distortion_mat,             // distCoeffs
		    cv::noArray(),              // rvecs
		    cv::noArray());             // tvecs
	}

	P("CALIBRATION DONE RP ERROR %f", rp_error);

	// clang-format off
	std::cout << "image_size: " << image_size << "\n";
	std::cout << "rp_error: " << rp_error << "\n";
	std::cout << "intrinsics_mat:\n" << intrinsics_mat << "\n";
	if (c.use_fisheye) {
		std::cout << "new_intrinsics_mat:\n" << new_intrinsics_mat << "\n";
		std::cout << "distortion_fisheye_mat:\n" << distortion_fisheye_mat << "\n";
	} else {
		std::cout << "distortion_mat:\n" << distortion_mat << "\n";
	}
	// clang-format on

	if (c.use_fisheye) {
		cv::fisheye::initUndistortRectifyMap(
		    intrinsics_mat,         // K
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
		    intrinsics_mat,          // P
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
		int num = (int)c.state.board_models.size();
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
do_capture_logic_mono(class Calibration &c,
                      struct ViewState &view,
                      bool found,
                      cv::Mat &gray,
                      cv::Mat &rgb)
{
	int num = (int)c.state.board_models.size();
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

		snprintf(buf, 512, "gray_%ix%i_%03i.png", gray.cols, gray.rows,
		         (int)view.measured.size());
		cv::imwrite(buf, gray);

		snprintf(buf, 512, "debug_rgb_%03i.jpg",
		         (int)view.measured.size());
		cv::imwrite(buf, rgb);
	}

	c.state.board_models.push_back(c.board.model);
	view.measured.push_back(view.current);
	view.measuredBounds.push_back(view.currentBounds);

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

	int num = (int)c.state.board_models.size();
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

		snprintf(buf, 512, "gray_%ix%i_%03i.png", gray.cols, gray.rows,
		         (int)c.state.board_models.size());
		cv::imwrite(buf, gray);

		snprintf(buf, 512, "debug_rgb_%03i.jpg",
		         (int)c.state.board_models.size());
		cv::imwrite(buf, rgb);
	}

	c.state.board_models.push_back(c.board.model);
	c.state.view[0].measured.push_back(c.state.view[0].current);
	c.state.view[0].measuredBounds.push_back(c.state.view[0].currentBounds);
	c.state.view[1].measured.push_back(c.state.view[1].current);
	c.state.view[1].measuredBounds.push_back(c.state.view[1].currentBounds);

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

	if (c.state.board_models.size() >= c.num_collect_total) {
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
	cv::Mat l_rgb(rows, cols, CV_8UC3, c.gui.frame->data,
	              c.gui.frame->stride);
	cv::Mat r_rgb(rows, cols, CV_8UC3, c.gui.frame->data + 3 * cols,
	              c.gui.frame->stride);

	bool found_left = do_view(c, c.state.view[0], l_gray, l_rgb);
	bool found_right = do_view(c, c.state.view[1], r_gray, r_rgb);

	do_capture_logic_stereo(c, gray, rgb, found_left, c.state.view[0],
	                        l_gray, l_rgb, found_right, c.state.view[1],
	                        r_gray, r_rgb);

	if (c.state.board_models.size() >= c.num_collect_total) {
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
		cv::Mat r_rgb(rows, cols, CV_8UC3, frame.data + 3 * cols,
		              frame.stride);

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

		snprintf(buf, 512, "gray_%ux%u_%03i.png", xf->width, xf->height,
		         i);
		c.gray = cv::imread(buf, cv::IMREAD_GRAYSCALE);

		if (c.gray.rows == 0 || c.gray.cols == 0) {
			fprintf(stderr, "Could not find image '%s'!\n", buf);
			continue;
		}

		if (c.gray.rows != (int)xf->height ||
		    c.gray.cols != (int)xf->width) {
			fprintf(stderr,
			        "Image size does not match frame size! Image: "
			        "(%ix%i) Frame: (%ux%u)\n",
			        c.gray.cols, c.gray.rows, xf->width,
			        xf->height);
			continue;
		}

		// Create a new RGB image and then copy the gray data to it.
		refresh_gui_frame(c, c.gray.rows, c.gray.cols);
		cv::cvtColor(c.gray, c.gui.rgb, cv::COLOR_GRAY2RGB);

#if 0
		xf->stereo_format = XRT_STEREO_FORMAT_SBS;
#endif

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
	case XRT_FORMAT_YUV422: process_frame_yuyv(c, xf); break;
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
		cv::rectangle(c.gui.rgb, cv::Point2f(0, 0),
		              cv::Point2f(c.gui.rgb.cols, c.gui.rgb.rows),
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
		c.board.spacing_meters =
		    params->asymmetric_circles.diagonal_distance_meters;
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

	// Ensure we only get yuv or yuyv frames.
	u_sink_create_to_yuv_or_yuyv(xfctx, *out_sink, out_sink);

	// Build the board model.
	build_board_position(c);

	// Pre allocate
	c.state.view[0].current.reserve(c.board.model.size());
	c.state.view[0].currentDisplay.reserve(c.board.model.size());
	c.state.view[1].current.reserve(c.board.model.size());
	c.state.view[1].currentDisplay.reserve(c.board.model.size());

#if 0
	c.state.view[0].measured = (ArrayOfMeasurements){
	};

	c.state.view[1].measured = (ArrayOfMeasurements){
	};

	for (Measurement &m : c.state.view[0].measured) {
		(void)m;
		c.state.board_models.push_back(c.board.model);
	}
#endif
	return ret;
}
