// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Calibration code.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "util/u_sink.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_format.h"
#include "tracking/t_tracking.h"

#include <opencv2/opencv.hpp>

DEBUG_GET_ONCE_BOOL_OPTION(hsv_filter, "T_DEBUG_HSV_FILTER", false)
DEBUG_GET_ONCE_BOOL_OPTION(hsv_picker, "T_DEBUG_HSV_PICKER", false)
DEBUG_GET_ONCE_BOOL_OPTION(hsv_viewer, "T_DEBUG_HSV_VIEWER", false)


/*
 *
 * Structs
 *
 */

class Calibration
{
public:
	struct xrt_frame_sink base = {};

	struct
	{
		cv::Mat rgb = {};
		struct xrt_frame_sink *sink = {};
	} gui;

	cv::Mat grey;

	char text[512];
};

/*!
 * Holds `cv::Mat`s used during frame processing when processing a yuyv frame.
 */
struct t_frame_yuyv
{
public:
	//! Full frame size, each block is split across two cols.
	cv::Mat data_full = {};
	//! Half horizontal width covering a complete block of two pixels.
	cv::Mat data_half = {};
};


/*
 *
 * Small helpers.
 *
 */

static void
send_rgb_frame(struct xrt_frame_sink *xsink, cv::Mat &rgb)
{
	struct xrt_frame xf = {};

	xf.format = XRT_FORMAT_R8G8B8;
	xf.width = rgb.cols;
	xf.height = rgb.rows;
	xf.data = rgb.data;

	u_format_size_for_dimensions(xf.format, xf.width, xf.height, &xf.stride,
	                             &xf.size);

	xsink->push_frame(xsink, &xf);
}

static void
ensure_buffers_are_allocated(class Calibration &c, int rows, int cols)
{
	if (c.gui.rgb.cols == cols && c.gui.rgb.rows == rows) {
		return;
	}

	c.grey = cv::Mat(rows, cols, CV_8UC1, cv::Scalar(0));
	c.gui.rgb = cv::Mat(rows, cols, CV_8UC3, cv::Scalar(0, 0, 0));
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

	send_rgb_frame(c.gui.sink, c.gui.rgb);
}

static void
make_calibration_frame(class Calibration &c)
{
	auto &rgb = c.gui.rgb;

	if (rgb.rows == 0 || rgb.cols == 0) {
		ensure_buffers_are_allocated(c, 480, 640);
		cv::rectangle(c.gui.rgb, cv::Point2f(0, 0),
		              cv::Point2f(rgb.cols, rgb.rows),
		              cv::Scalar(0, 0, 0), -1, 0);
	}

	/*
	 * Draw text
	 */

	print_txt(rgb, "CALIBRATION MODE", 1.5);

	send_rgb_frame(c.gui.sink, rgb);
}


/*
 *
 * Main functions.
 *
 */

static void
process_frame_yuv(class Calibration &c, struct xrt_frame *xf)
{

	int w = (int)xf->width;
	int h = (int)xf->height;

	cv::Mat data(h, w, CV_8UC3, xf->data, xf->stride);
	ensure_buffers_are_allocated(c, data.rows, data.cols);

	cv::cvtColor(data, c.gui.rgb, cv::COLOR_YUV2RGB);
	cv::cvtColor(c.gui.rgb, c.grey, cv::COLOR_RGB2GRAY);
}

static void
process_frame_yuyv(class Calibration &c, struct xrt_frame *xf)
{
	/*
	 * Cleverly extract the different channels.
	 * Cr/Cb are extracted at half width.
	 */
	int w = (int)xf->width;
	int half_w = w / 2;
	int h = (int)xf->height;

	struct t_frame_yuyv f = {};

	f.data_half = cv::Mat(h, half_w, CV_8UC4, xf->data, xf->stride);
	f.data_full = cv::Mat(h, w, CV_8UC2, xf->data, xf->stride);
	ensure_buffers_are_allocated(c, f.data_full.rows, f.data_full.cols);

	cv::cvtColor(f.data_full, c.gui.rgb, cv::COLOR_YUV2RGB_YUYV);
	cv::cvtColor(f.data_full, c.grey, cv::COLOR_YUV2GRAY_YUYV);
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

#if 0
	if (xf->stereo_format != XRT_FS_STEREO_SBS) {
		snprintf(c.text, sizeof(c.text),
		         "ERROR: Not side by side stereo!");
		make_gui_str(c);
		return;
	}
#endif

	// Fill both c.gui.rgb and c.grey with the data we got.
	switch (xf->format) {
	case XRT_FORMAT_YUV888: process_frame_yuv(c, xf); break;
	case XRT_FORMAT_YUV422: process_frame_yuyv(c, xf); break;
	default:
		snprintf(c.text, sizeof(c.text), "ERROR: Bad format '%s'",
		         u_format_str(xf->format));
		make_gui_str(c);
		return;
	}

	make_calibration_frame(c);
}


/*
 *
 * Exported functions.
 *
 */

extern "C" int
t_calibration_create(struct xrt_frame_sink *gui,
                     struct xrt_frame_sink **out_sink)
{

	auto &c = *(new Calibration());

	c.gui.sink = gui;

	c.base.push_frame = t_calibration_frame;

	*out_sink = &c.base;

	snprintf(c.text, sizeof(c.text), "Waiting for camera");
	make_gui_str(c);

	int ret = 0;
	if (debug_get_bool_option_hsv_filter()) {
		ret = t_debug_hsv_filter_create(*out_sink, out_sink);
	}

	if (debug_get_bool_option_hsv_picker()) {
		ret = t_debug_hsv_picker_create(*out_sink, out_sink);
	}

	if (debug_get_bool_option_hsv_viewer()) {
		ret = t_debug_hsv_viewer_create(*out_sink, out_sink);
	}

	// Ensure we only get yuv or yuyv frames.
	u_sink_create_to_yuv_or_yuyv(*out_sink, out_sink);

	return ret;
}
