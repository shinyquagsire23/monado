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

// we will use a number of samples spread across the frame
// to ensure a good calibration. must be > 9
#define CALIBRATION_SAMPLES 15

// set up our calibration rectangles, we will collect 9 chessboard samples
// that 'fill' these rectangular regions to get good coverage
#define COVERAGE_X 0.8f
#define COVERAGE_Y 0.8f

static cv::Rect2f calibration_rect[] = {
    cv::Rect2f(
        (1.0f - COVERAGE_X) / 2.0f, (1.0f - COVERAGE_Y) / 2.0f, 0.3f, 0.3f),
    cv::Rect2f((1.0f - COVERAGE_X) / 2.0f + COVERAGE_X / 3.0f,
               (1.0f - COVERAGE_Y) / 2.0f,
               0.3f,
               0.3f),
    cv::Rect2f((1.0f - COVERAGE_X) / 2.0f + 2 * COVERAGE_X / 3.0f,
               (1.0f - COVERAGE_Y) / 2.0f,
               0.3f,
               0.3f),

    cv::Rect2f((1.0f - COVERAGE_X) / 2.0f,
               (1.0f - COVERAGE_Y) / 2.0f + COVERAGE_Y / 3.0f,
               0.3f,
               0.3f),
    cv::Rect2f((1.0f - COVERAGE_X) / 2.0f + COVERAGE_X / 3.0f,
               (1.0f - COVERAGE_Y) / 2.0f + COVERAGE_Y / 3.0f,
               0.3f,
               0.3f),
    cv::Rect2f((1.0f - COVERAGE_X) / 2.0f + 2 * COVERAGE_X / 3.0f,
               (1.0f - COVERAGE_Y) / 2.0f + COVERAGE_Y / 3.0f,
               0.3f,
               0.3f),

    cv::Rect2f((1.0f - COVERAGE_X) / 2.0f,
               (1.0f - COVERAGE_Y) / 2.0f + 2 * COVERAGE_Y / 3.0f,
               0.3f,
               0.3f),
    cv::Rect2f((1.0f - COVERAGE_X) / 2.0f + COVERAGE_X / 3.0f,
               (1.0f - COVERAGE_Y) / 2.0f + 2 * COVERAGE_Y / 3.0f,
               0.3f,
               0.3f),
    cv::Rect2f((1.0f - COVERAGE_X) / 2.0f + 2 * COVERAGE_X / 3.0f,
               (1.0f - COVERAGE_Y) / 2.0f + 2 * COVERAGE_Y / 3.0f,
               0.3f,
               0.3f),
};


/*
 *
 * Structs
 *
 */

//! Model of the thing we are measuring to calibrate.
typedef std::vector<cv::Point3f> Model;
//! A measurement of the model as viewed on the camera.
typedef std::vector<cv::Point2f> Measurement;
//! For each @ref Measurement we take we also save the @ref Model.
typedef std::vector<Model> ArrayOfModels;
//! A array of @ref Measurement.
typedef std::vector<Measurement> ArrayOfMeasurements;

/*!
 * Current state for each view, one view for mono cameras, two for stereo.
 */
struct ViewState
{
	ArrayOfMeasurements measured = {};

	bool last_valid = false;
	Measurement last;

	Measurement current = {};

	cv::Rect brect = {};
	cv::Rect pre_rect = {};
	cv::Rect post_rect = {};

	bool maps_valid = false;
	cv::Mat map1 = {};
	cv::Mat map2 = {};
};

/*!
 * What type of pattern is being used.
 */
enum class Pattern
{
	CHESSBOARD,
	CIRCLES_GRID,
	ASYMMETRIC_CIRCLES_GRID,
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
		Pattern pattern = Pattern::CHESSBOARD;
		float spacing_meters = 0.05;
	} board;

	struct
	{
		ViewState view[2] = {};

		ArrayOfModels board_models = {};

		uint32_t calibration_count = {};
		bool calibrated = false;


		uint32_t waited_for = 0;
		uint32_t collected_of_part = 0;
	} state;

	//! Should we use subpixel enhancing for checkerboard.
	bool subpixel_enable = true;
	//! What subpixel range for checkerboard enhancement.
	int subpixel_size = 5;

	//! Number of frames to wait for before collecting.
	uint32_t num_wait_for = 20;
	//! Total number of samples to collect.
	uint32_t num_collect_total = 40;
	//! Number of frames to capture before restarting.
	uint32_t num_collect_restart = 1;

	//! Is the camera fisheye.
	bool use_fisheye = false;

	//! Should we clear the frame.
	bool clear_frame = false;

	//! Dump all of the measurements to stdout.
	bool dump_measurements = false;

	//! Should we save images used for capture.
	bool save_images;

	cv::Mat grey = {};

	char text[512] = {};
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

	c.grey = cv::Mat(rows, cols, CV_8UC1, cv::Scalar(0));

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

static bool
do_view_chess(class Calibration &c,
              struct ViewState &view,
              cv::Mat &grey,
              cv::Mat &rgb)
{
	int flags = 0;
	flags += cv::CALIB_CB_FAST_CHECK;
	flags += cv::CALIB_CB_ADAPTIVE_THRESH;
	flags += cv::CALIB_CB_NORMALIZE_IMAGE;

	bool found = cv::findChessboardCorners(grey,         // Image
	                                       c.board.dims, // patternSize
	                                       view.current, // corners
	                                       flags);       // flags

	// Compute our 'pre sample' coverage for this frame,
	// for display and area threshold checking.
	std::vector<cv::Point2f> coverage;
	for (uint32_t i = 0; i < view.measured.size(); i++) {
		cv::Rect brect = cv::boundingRect(view.measured[i]);

		draw_rect(rgb, brect, cv::Scalar(0, 64, 32));

		coverage.push_back(cv::Point2f(brect.tl()));
		coverage.push_back(cv::Point2f(brect.br()));
	}

	// What area of the camera have we calibrated.
	view.pre_rect = cv::boundingRect(coverage);
	draw_rect(rgb, view.pre_rect, cv::Scalar(0, 255, 255));

	if (found) {
		view.brect = cv::boundingRect(view.current);
		coverage.push_back(cv::Point2f(view.brect.tl()));
		coverage.push_back(cv::Point2f(view.brect.br()));

		// New area we cover.
		view.post_rect = cv::boundingRect(coverage);

		draw_rect(rgb, view.post_rect, cv::Scalar(0, 255, 0));
	}

	// Improve the corner positions.
	if (found && c.subpixel_enable) {
		int crit_flag = 0;
		crit_flag |= cv::TermCriteria::EPS;
		crit_flag |= cv::TermCriteria::COUNT;
		cv::TermCriteria term_criteria = {crit_flag, 30, 0.1};

		cv::Size size(c.subpixel_size, c.subpixel_size);
		cv::Size zero(-1, -1);

		cv::cornerSubPix(grey, view.current, size, zero, term_criteria);
	}

	// Draw the checker board, will also draw partial hits.
	cv::drawChessboardCorners(rgb, c.board.dims, view.current, found);

	return found;
}

static bool
do_view_circles(class Calibration &c,
                struct ViewState &view,
                cv::Mat &grey,
                cv::Mat &rgb)
{
	int flags = 0;
	if (c.board.pattern == Pattern::ASYMMETRIC_CIRCLES_GRID) {
		flags |= cv::CALIB_CB_ASYMMETRIC_GRID;
	}

	bool found = cv::findCirclesGrid(grey,         // Image
	                                 c.board.dims, // patternSize
	                                 view.current, // corners
	                                 flags);       // flags

	// Compute our 'pre sample' coverage for this frame,
	// for display and area threshold checking.
	std::vector<cv::Point2f> coverage;
	for (uint32_t i = 0; i < view.measured.size(); i++) {
		cv::Rect brect = cv::boundingRect(view.measured[i]);

		draw_rect(rgb, brect, cv::Scalar(0, 64, 32));

		coverage.push_back(cv::Point2f(brect.tl()));
		coverage.push_back(cv::Point2f(brect.br()));
	}

	// What area of the camera have we calibrated.
	view.pre_rect = cv::boundingRect(coverage);
	draw_rect(rgb, view.pre_rect, cv::Scalar(0, 255, 255));

	if (found) {
		view.brect = cv::boundingRect(view.current);
		coverage.push_back(cv::Point2f(view.brect.tl()));
		coverage.push_back(cv::Point2f(view.brect.br()));

		// New area we cover.
		view.post_rect = cv::boundingRect(coverage);

		draw_rect(rgb, view.post_rect, cv::Scalar(0, 255, 0));
	}

	// Draw the circles we found, will also draw partial hits.
	cv::drawChessboardCorners(rgb, c.board.dims, view.current, found);

	return found;
}

static bool
do_view(class Calibration &c,
        struct ViewState &view,
        cv::Mat &grey,
        cv::Mat &rgb)
{
	switch (c.board.pattern) {
	case Pattern::CHESSBOARD: return do_view_chess(c, view, grey, rgb);
	case Pattern::CIRCLES_GRID: return do_view_circles(c, view, grey, rgb);
	case Pattern::ASYMMETRIC_CIRCLES_GRID:
		return do_view_circles(c, view, grey, rgb);
	default: assert(false);
	}
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
	case Pattern::CHESSBOARD:
	case Pattern::CIRCLES_GRID:
		// Nothing to do.
		break;
	case Pattern::ASYMMETRIC_CIRCLES_GRID:
		// From diagonal size to "square" size.
		size_meters = sqrt((size_meters * size_meters) / 2.0);
		break;
	}

	switch (c.board.pattern) {
	case Pattern::CHESSBOARD:
	case Pattern::CIRCLES_GRID:
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
	case Pattern::ASYMMETRIC_CIRCLES_GRID:
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

/*
 *
 * Stereo calibration
 *
 */

#define P(...) snprintf(c.text, sizeof(c.text), __VA_ARGS__)

static void
process_stereo_samples(class Calibration &c, int cols, int rows)
{
	c.state.calibrated = true;

	cv::Size image_size(cols, rows);
	cv::Size new_image_size(cols, rows);

	CalibrationRawData raw = {};
	assert(raw.isDataStorageValid());

	raw.image_size_pixels.w = image_size.width;
	raw.image_size_pixels.h = image_size.height;
	raw.new_image_size_pixels.w = new_image_size.width;
	raw.new_image_size_pixels.h = new_image_size.height;

	// TODO: handle both fisheye and normal cameras -right
	// now I only have the normal, for the PS4 camera
#if 0
	float rp_error = cv::fisheye::stereoCalibrate(
	    internal->board_models, internal->l_measured,
	    internal->r_measured, l_intrinsics,
	    l_distortion_fisheye, r_intrinsics, r_distortion_fisheye,
	    image_size, camera_rotation, camera_translation,
	    cv::fisheye::CALIB_RECOMPUTE_EXTRINSIC);
#endif

	// non-fisheye version
	float rp_error =
	    cv::stereoCalibrate(c.state.board_models,       // objectPoints
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

	assert(raw.camera_rotation_mat.size() == cv::Size(3, 3));
	assert(raw.camera_translation_mat.size() == cv::Size(1, 3));
	assert(raw.camera_essential_mat.size() == cv::Size(3, 3));
	assert(raw.camera_fundamental_mat.size() == cv::Size(3, 3));

	// We currently don't change the image size or remove invalid pixels.
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

	// Validate that nothing has been re-allocated.
	assert(raw.isDataStorageValid());

	P("CALIBRATION DONE RP ERROR %f", rp_error);

	// clang-format off
	std::cout << "calibration rp_error: " << rp_error << "\n";
	std::cout << "camera_rotation:\n" << raw.camera_rotation_mat << "\n";
	std::cout << "camera_translation:\n" << raw.camera_translation_mat << "\n";
	std::cout << "camera_essential:\n" << raw.camera_essential_mat << "\n";
	std::cout << "camera_fundamental:\n" << raw.camera_fundamental_mat << "\n";
	// clang-format on

	t_file_save_raw_data_hack(&raw);
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
			for (cv::Point2f &p : m) {
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
		flags |= cv::fisheye::CALIB_FIX_PRINCIPAL_POINT;

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

		double balance = 0.0f;

		cv::fisheye::estimateNewCameraMatrixForUndistortRectify(
		    intrinsics_mat,         // K
		    distortion_fisheye_mat, // D
		    image_size,             // image_size
		    cv::Matx33d::eye(),     // R
		    new_intrinsics_mat,     // P
		    balance);               // balance
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

	P("Calibration done, RP-Error %f", rp_error);

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

/*!
 * Logic for capturing a frame.
 */
static void
do_capture_logic(class Calibration &c,
                 struct ViewState &view,
                 bool found,
                 cv::Mat &grey,
                 cv::Mat &rgb)
{
	int num = (int)c.state.board_models.size();
	int of = c.num_collect_total;
	P("(%i/%i) SHOW CHESSBOARD", num, of);

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

		bool moved = false;
		if (view.last_valid) {
			moved = has_measurement_moved(view.last, view.current);
		}

		// Now save the current measurement to the last one.
		view.last = view.current;
		view.last_valid = true;

		if (moved) {
			P("(%i/%i) KEEP BOARD STILL!", num, of);
			c.state.waited_for = c.num_wait_for;
			c.state.collected_of_part = 0;
		}

		return;
	}

	// Have we collected all of the frames for one part?
	if (c.state.collected_of_part >= c.num_collect_restart) {
		c.state.waited_for = c.num_wait_for * 2;
		c.state.collected_of_part = 0;
		return;
	}

	if (c.save_images) {
		char buf[512];

		snprintf(buf, 512, "grey_%03i.png", (int)view.measured.size());
		cv::imwrite(buf, grey);

		snprintf(buf, 512, "debug_rgb_%03i.jpg",
		         (int)view.measured.size());
		cv::imwrite(buf, rgb);
	}

	c.state.board_models.push_back(c.board.model);
	view.measured.push_back(view.current);

	c.state.collected_of_part++;

	P("(%i/%i) COLLECTED #%i", num, of, c.state.collected_of_part);
}

/*!
 * Make a mono frame.
 */
static void
make_calibration_frame_mono(class Calibration &c)
{
	auto &rgb = c.gui.rgb;
	auto &grey = c.grey;

	bool found = do_view(c, c.state.view[0], grey, rgb);

	// Advance the state of the calibration.
	do_capture_logic(c, c.state.view[0], found, grey, rgb);

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
	auto &grey = c.grey;

	int cols = rgb.cols / 2;
	int rows = rgb.rows;

	// Split left and right eyes, don't make any copies.
	cv::Mat l_grey(rows, cols, CV_8UC1, grey.data, grey.cols);
	cv::Mat r_grey(rows, cols, CV_8UC1, grey.data + cols, grey.cols);
	cv::Mat l_rgb(rows, cols, CV_8UC3, c.gui.frame->data,
	              c.gui.frame->stride);
	cv::Mat r_rgb(rows, cols, CV_8UC3, c.gui.frame->data + 3 * cols,
	              c.gui.frame->stride);

	bool found_left = do_view(c, c.state.view[0], l_grey, l_rgb);
	bool found_right = do_view(c, c.state.view[1], r_grey, r_rgb);

	// Draw our current calibration guide box.
	cv::Point2f bound_tl = calibration_rect[c.state.calibration_count].tl();
	bound_tl.x *= cols;
	bound_tl.y *= rows;

	cv::Point2f bound_br = calibration_rect[c.state.calibration_count].br();
	bound_br.x *= cols;
	bound_br.y *= rows;

	// Draw the target rect last so it is the most visible.
	cv::rectangle(c.gui.rgb, bound_tl, bound_br, cv::Scalar(255, 0, 0));

	// If we have a valid sample (left and right).
	if (found_left && found_right) {
		cv::Rect brect = c.state.view[0].brect;
		cv::Rect pre_rect = c.state.view[0].pre_rect;
		cv::Rect post_rect = c.state.view[0].post_rect;

		/*
		 * Determine if we should add this sample to our list. Either we
		 * are still taking the first 9 samples and the chessboard is in
		 * the box, or we have exceeded 9 samples and now want to 'push
		 * out the edges'.
		 */

		bool add_sample = false;
		int coverage_threshold = cols * 0.3f * rows * 0.3f;

		if (c.state.calibration_count < 9 &&
		    brect.tl().x >= bound_tl.x && brect.tl().y >= bound_tl.y &&
		    brect.br().x <= bound_br.x && brect.br().y <= bound_br.y) {
			add_sample = true;
		}

		if (c.state.calibration_count >= 9 &&
		    brect.area() > coverage_threshold &&
		    post_rect.area() >
		        pre_rect.area() + coverage_threshold / 5) {
			add_sample = true;
		}

		if (add_sample) {
			c.state.board_models.push_back(c.board.model);
			c.state.view[0].measured.push_back(
			    c.state.view[0].current);
			c.state.view[1].measured.push_back(
			    c.state.view[1].current);
			c.state.calibration_count++;

			printf("SAMPLE: %ld\n",
			       c.state.view[0].measured.size());
		}
	}

	// Are we done or do we need to inform the user what they should do.
	if (c.state.calibration_count >= CALIBRATION_SAMPLES) {
		process_stereo_samples(c, cols, rows);
	} else if (c.state.calibration_count < 9) {
		P("POSITION CHESSBOARD IN BOX");
	} else {
		P("TRY TO 'PUSH OUT EDGES' WITH LARGE BOARD IMAGES");
	}

	// Draw text and finally send the frame off.
	print_txt(rgb, c.text, 1.5);
	send_rgb_frame(c);
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
	cv::cvtColor(c.gui.rgb, c.grey, cv::COLOR_RGB2GRAY);
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
	cv::cvtColor(data_full, c.grey, cv::COLOR_YUV2GRAY_YUYV);
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

	// Fill both c.gui.rgb and c.grey with the data we got.
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
		//! @todo add support for stereo.
		remap_view(c, c.state.view[0], c.gui.rgb);

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

	switch (xf->stereo_format) {
	case XRT_STEREO_FORMAT_SBS: make_calibration_frame_sbs(c); break;
	case XRT_STEREO_FORMAT_NONE: make_calibration_frame_mono(c); break;
	default:
		P("ERROR: Unknown stereo format! '%i'", xf->stereo_format);
		make_gui_str(c);
		return;
	}
}


/*
 *
 * Exported functions.
 *
 */

extern "C" int
t_calibration_stereo_create(struct xrt_frame_context *xfctx,
                            struct t_calibration_params *params,
                            struct xrt_frame_sink *gui,
                            struct xrt_frame_sink **out_sink)
{
	auto &c = *(new Calibration());

	// Basic setup.
	c.gui.sink = gui;
	c.base.push_frame = t_calibration_frame;
	*out_sink = &c.base;

	// Copy the parameters.
	c.board.dims = {
	    params->checker_cols_num - 1,
	    params->checker_rows_num - 1,
	};
	c.board.spacing_meters = params->checker_size_meters;
	c.subpixel_enable = params->subpixel_enable;
	c.subpixel_size = params->subpixel_size;
	c.num_wait_for = params->num_wait_for;
	c.num_collect_total = params->num_collect_total;
	c.num_collect_restart = params->num_collect_restart;
	c.save_images = params->save_images;
	c.use_fisheye = params->use_fisheye;

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
