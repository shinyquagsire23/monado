// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  HSV Picker Debugging code.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_format.h"

#include "tracking/t_tracking.h"

#include <opencv2/opencv.hpp>


/*
 *
 * Defines and structs
 *
 */

#define PICK_WIN "HSV Picker Debugger"

#define max(a, b) (a > b ? a : b)
#define min(a, b) (a < b ? a : b)

/*!
 * An @ref xrt_frame_sink that can be used to select HSV thresholds
 * interactively.
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
class DebugHSVPicker
{
public:
	struct xrt_frame_sink base = {};
	struct xrt_frame_node node = {};

	struct
	{
		cv::Mat hsv = {};
		cv::Mat threshold = {};
	} debug;

	struct xrt_frame_sink *passthrough;

	struct t_convert_table yuv_to_hsv;
};

const int max_value_H = 360 / 2;
const int max_value = 256;
static int low_H = 0, low_S = 0, low_V = 0;
static int high_H = max_value_H, high_S = max_value, high_V = max_value;


/*
 *
 * Debug functions.
 *
 */

static void
ensure_debug_is_allocated(class DebugHSVPicker &d, int rows, int cols)
{
	if (d.debug.hsv.cols == cols && d.debug.hsv.rows == rows) {
		return;
	}

	d.debug.threshold = cv::Mat(rows, cols, CV_8UC1);
	d.debug.hsv = cv::Mat(rows, cols, CV_8UC3);
}

static void
process_frame_yuv(class DebugHSVPicker &d, struct xrt_frame *xf)
{
	for (uint32_t y = 0; y < xf->height; y++) {
		uint8_t *src = (uint8_t *)xf->data + y * xf->stride;
		auto hsv = d.debug.hsv.ptr<uint8_t>(y);
		for (uint32_t x = 0; x < xf->width; x++) {
			uint8_t y = src[0];
			uint8_t cb = src[1];
			uint8_t cr = src[2];

			uint8_t *hsv1 = d.yuv_to_hsv.v[y][cb][cr];

			hsv[0] = hsv1[0];
			hsv[1] = hsv1[1];
			hsv[2] = hsv1[2];

			hsv += 3;
			src += 3;
		}
	}

	cv::inRange(d.debug.hsv, cv::Scalar(low_H, low_S, low_V), cv::Scalar(high_H, high_S, high_V),
	            d.debug.threshold);
	cv::imshow(PICK_WIN, d.debug.threshold);
}

static void
process_frame_yuyv(class DebugHSVPicker &d, struct xrt_frame *xf)
{
	for (uint32_t y = 0; y < xf->height; y++) {
		uint8_t *src = (uint8_t *)xf->data + y * xf->stride;
		auto hsv = d.debug.hsv.ptr<uint8_t>(y);
		for (uint32_t x = 0; x < xf->width; x += 2) {
			uint8_t y1 = src[0];
			uint8_t cb = src[1];
			uint8_t y2 = src[2];
			uint8_t cr = src[3];

			uint8_t *hsv1 = d.yuv_to_hsv.v[y1][cb][cr];
			uint8_t *hsv2 = d.yuv_to_hsv.v[y2][cb][cr];

			hsv[0] = hsv1[0];
			hsv[1] = hsv1[1];
			hsv[2] = hsv1[2];
			hsv[3] = hsv2[0];
			hsv[4] = hsv2[1];
			hsv[5] = hsv2[2];

			hsv += 6;
			src += 4;
		}
	}

	cv::inRange(d.debug.hsv, cv::Scalar(low_H, low_S, low_V), cv::Scalar(high_H, high_S, high_V),
	            d.debug.threshold);
	cv::imshow(PICK_WIN, d.debug.threshold);
}

static void
process_frame(class DebugHSVPicker &d, struct xrt_frame *xf)
{
	ensure_debug_is_allocated(d, xf->height, xf->width);

	switch (xf->format) {
	case XRT_FORMAT_YUV888: process_frame_yuv(d, xf); break;
	case XRT_FORMAT_YUYV422: process_frame_yuyv(d, xf); break;
	default: U_LOG_E("Bad format '%s'", u_format_str(xf->format)); break;
	}
}

static void
on_low_H_thresh_trackbar(int /*unused*/, void * /*unused*/)
{
	low_H = min(high_H - 1, low_H);
	cv::setTrackbarPos("Low H", PICK_WIN, low_H);
}

static void
on_high_H_thresh_trackbar(int /*unused*/, void * /*unused*/)
{
	high_H = max(high_H, low_H + 1);
	cv::setTrackbarPos("High H", PICK_WIN, high_H);
}

static void
on_low_S_thresh_trackbar(int /*unused*/, void * /*unused*/)
{
	low_S = min(high_S - 1, low_S);
	cv::setTrackbarPos("Low S", PICK_WIN, low_S);
}

static void
on_high_S_thresh_trackbar(int /*unused*/, void * /*unused*/)
{
	high_S = max(high_S, low_S + 1);
	cv::setTrackbarPos("High S", PICK_WIN, high_S);
}

static void
on_low_V_thresh_trackbar(int /*unused*/, void * /*unused*/)
{
	low_V = min(high_V - 1, low_V);
	cv::setTrackbarPos("Low V", PICK_WIN, low_V);
}

static void
on_high_V_thresh_trackbar(int /*unused*/, void * /*unused*/)
{
	high_V = max(high_V, low_V + 1);
	cv::setTrackbarPos("High V", PICK_WIN, high_V);
}


/*
 *
 * Exported functions.
 *
 */

extern "C" void
t_debug_hsv_picker_frame(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	auto &d = *(class DebugHSVPicker *)xsink;

	process_frame(d, xf);

	d.passthrough->push_frame(d.passthrough, xf);
}

extern "C" void
t_debug_hsv_picker_break_apart(struct xrt_frame_node *node)
{}

extern "C" void
t_debug_hsv_picker_destroy(struct xrt_frame_node *node)
{
	auto d = container_of(node, DebugHSVPicker, node);
	delete d;
}

extern "C" int
t_debug_hsv_picker_create(struct xrt_frame_context *xfctx,
                          struct xrt_frame_sink *passthrough,
                          struct xrt_frame_sink **out_sink)
{
	auto &d = *(new DebugHSVPicker());

	cv::namedWindow(PICK_WIN);

	d.base.push_frame = t_debug_hsv_picker_frame;
	d.node.break_apart = t_debug_hsv_picker_break_apart;
	d.node.destroy = t_debug_hsv_picker_destroy;
	d.passthrough = passthrough;

	// Trackbars to set thresholds for HSV values
	cv::createTrackbar("Low H", PICK_WIN, &low_H, max_value_H, on_low_H_thresh_trackbar);
	cv::createTrackbar("High H", PICK_WIN, &high_H, max_value_H, on_high_H_thresh_trackbar);
	cv::createTrackbar("Low S", PICK_WIN, &low_S, max_value, on_low_S_thresh_trackbar);
	cv::createTrackbar("High S", PICK_WIN, &high_S, max_value, on_high_S_thresh_trackbar);
	cv::createTrackbar("Low V", PICK_WIN, &low_V, max_value, on_low_V_thresh_trackbar);
	cv::createTrackbar("High V", PICK_WIN, &high_V, max_value, on_high_V_thresh_trackbar);

	cv::startWindowThread();

	t_convert_make_y8u8v8_to_h8s8v8(&d.yuv_to_hsv);

	xrt_frame_context_add(xfctx, &d.node);

	*out_sink = &d.base;

	return 0;
}
