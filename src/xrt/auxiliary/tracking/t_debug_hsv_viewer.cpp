// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  HSV debug viewer code.
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

#define HSV_WIN "HSV Filter Tester"

/*!
 * An @ref xrt_frame_sink related to debug viewing of HSV.
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
class DebugHSVViewer
{
public:
	struct xrt_frame_sink base = {};
	struct xrt_frame_node node = {};

	struct xrt_frame_sink *passthrough;

	cv::Mat bgr;

	int lum_value = 0;

	// No need to initialize these
	struct t_convert_table yuv_to_rgb_table;
	struct t_hsv_filter_large_table hsv_large;
	struct t_hsv_filter_optimized_table hsv_opt;
};


/*
 *
 * Debug functions.
 *
 */

static void
process_pixel(bool f1, bool f1_diff, uint8_t *hsv_cap, uint8_t *hsv_opt, uint8_t *hsv_diff, const uint8_t *rgb)
{
	if (f1) {
		hsv_cap[0] = rgb[2];
		hsv_cap[1] = rgb[1];
		hsv_cap[2] = rgb[0];
	} else {
		hsv_cap[0] = 0;
		hsv_cap[1] = 0;
		hsv_cap[2] = 0;
	}

	if (f1_diff) {
		hsv_opt[0] = rgb[2];
		hsv_opt[1] = rgb[1];
		hsv_opt[2] = rgb[0];
	} else {
		hsv_opt[0] = 0;
		hsv_opt[1] = 0;
		hsv_opt[2] = 0;
	}

	if (f1 > f1_diff) {
		hsv_diff[0] = 0xff;
		hsv_diff[1] = 0;
		hsv_diff[2] = 0;
	} else if (f1 < f1_diff) {
		hsv_diff[0] = 0;
		hsv_diff[1] = 0;
		hsv_diff[2] = 0xff;
	} else {
		hsv_diff[0] = 0;
		hsv_diff[1] = 0;
		hsv_diff[2] = 0;
	}
}

#define SIZE 256
#define NUM_CHAN 4

static void
process_frame(DebugHSVViewer &d, struct xrt_frame *xf)
{
	uint32_t width = SIZE * 3;
	uint32_t height = SIZE * NUM_CHAN;

	auto &bgr = d.bgr;
	if (bgr.rows != (int)height || bgr.cols != (int)width) {
		bgr = cv::Mat(height, width, CV_8UC3);
	}

	for (uint32_t yp = 0; yp < SIZE; yp++) {
		for (int chan = 0; chan < NUM_CHAN; chan++) {
			auto hsv_cap = bgr.ptr<uint8_t>(yp + SIZE * chan);
			auto hsv_opt = bgr.ptr<uint8_t>(yp + SIZE * chan) + 256 * 3;
			auto hsv_diff = bgr.ptr<uint8_t>(yp + SIZE * chan) + 512 * 3;
			int mask = 1 << chan;

			for (uint32_t xp = 0; xp < SIZE; xp++) {
				int y = d.lum_value;
				int u = yp;
				int v = xp;

				uint8_t *rgb = d.yuv_to_rgb_table.v[y][u][v];
				uint8_t large = d.hsv_large.v[y][u][v];
				uint8_t opt = t_hsv_filter_sample(&d.hsv_opt, y, u, v);

				large = (large & mask) != 0;
				opt = (opt & mask) != 0;

				process_pixel(large, opt, hsv_cap, hsv_opt, hsv_diff, rgb);

				hsv_cap += 3;
				hsv_opt += 3;
				hsv_diff += 3;
			}
		}
	}

	cv::imshow(HSV_WIN, bgr);
}


/*
 *
 * Exported functions.
 *
 */

extern "C" void
t_debug_hsv_viewer_frame(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	auto &d = *(class DebugHSVViewer *)xsink;

	process_frame(d, xf);

	d.passthrough->push_frame(d.passthrough, xf);
}

extern "C" void
t_debug_hsv_viewer_break_apart(struct xrt_frame_node *node)
{}

extern "C" void
t_debug_hsv_viewer_destroy(struct xrt_frame_node *node)
{
	auto d = container_of(node, DebugHSVViewer, node);
	delete d;
}

extern "C" int
t_debug_hsv_viewer_create(struct xrt_frame_context *xfctx,
                          struct xrt_frame_sink *passthrough,
                          struct xrt_frame_sink **out_sink)
{
	auto &d = *(new DebugHSVViewer());

	cv::namedWindow(HSV_WIN);

	cv::createTrackbar("Luma", HSV_WIN, &d.lum_value, 255, NULL);

	cv::startWindowThread();

	d.base.push_frame = t_debug_hsv_viewer_frame;
	d.node.break_apart = t_debug_hsv_viewer_break_apart;
	d.node.destroy = t_debug_hsv_viewer_destroy;
	d.passthrough = passthrough;

	t_convert_make_y8u8v8_to_r8g8b8(&d.yuv_to_rgb_table);
	struct t_hsv_filter_params params = T_HSV_DEFAULT_PARAMS();
	t_hsv_build_large_table(&params, &d.hsv_large);
	t_hsv_build_optimized_table(&params, &d.hsv_opt);

	xrt_frame_context_add(xfctx, &d.node);

	*out_sink = &d.base;

	return 0;
}
