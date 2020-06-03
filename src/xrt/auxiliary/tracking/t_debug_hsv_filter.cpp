// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  HSV filter debug code.
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

#define HSV0_WIN "HSV Channel #1 (Red)"
#define HSV1_WIN "HSV Channel #2 (Purple)"
#define HSV2_WIN "HSV Channel #3 (Blue)"
#define HSV3_WIN "HSV Channel #4 (White)"

/*!
 * An @ref xrt_frame_sink that can be used to debug the behavior of
 * @ref t_hsv_filter.
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
class DebugHSVFilter
{
public:
	struct xrt_frame_sink base = {};
	struct xrt_frame_node node = {};
	struct xrt_frame_sink sinks[4] = {};

	struct xrt_frame_sink *sink;
	struct xrt_frame_sink *passthrough;
};


/*
 *
 * Exported functions.
 *
 */

extern "C" void
t_debug_hsv_filter_frame0(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	cv::Mat tmp(xf->height, xf->width, CV_8UC1, xf->data, xf->stride);

	cv::imshow(HSV0_WIN, tmp);
}

extern "C" void
t_debug_hsv_filter_frame1(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	cv::Mat tmp(xf->height, xf->width, CV_8UC1, xf->data, xf->stride);

	cv::imshow(HSV1_WIN, tmp);
}

extern "C" void
t_debug_hsv_filter_frame2(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	cv::Mat tmp(xf->height, xf->width, CV_8UC1, xf->data, xf->stride);

	cv::imshow(HSV2_WIN, tmp);
}

extern "C" void
t_debug_hsv_filter_frame3(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	cv::Mat tmp(xf->height, xf->width, CV_8UC1, xf->data, xf->stride);

	cv::imshow(HSV3_WIN, tmp);
}

extern "C" void
t_debug_hsv_filter_frame(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	auto &d = *(class DebugHSVFilter *)xsink;

	d.sink->push_frame(d.sink, xf);
	d.passthrough->push_frame(d.passthrough, xf);
}

extern "C" void
t_debug_hsv_filter_break_apart(struct xrt_frame_node *node)
{}

extern "C" void
t_debug_hsv_filter_destroy(struct xrt_frame_node *node)
{
	auto d = container_of(node, DebugHSVFilter, node);
	delete d;
}

extern "C" int
t_debug_hsv_filter_create(struct xrt_frame_context *xfctx,
                          struct xrt_frame_sink *passthrough,
                          struct xrt_frame_sink **out_sink)
{
	auto &d = *(new DebugHSVFilter());

	cv::namedWindow(HSV0_WIN);
	cv::namedWindow(HSV1_WIN);
	cv::namedWindow(HSV2_WIN);
	cv::namedWindow(HSV3_WIN);

	cv::startWindowThread();

	d.base.push_frame = t_debug_hsv_filter_frame;
	d.node.break_apart = t_debug_hsv_filter_break_apart;
	d.node.destroy = t_debug_hsv_filter_destroy;
	d.passthrough = passthrough;
	d.sinks[0].push_frame = t_debug_hsv_filter_frame0;
	d.sinks[1].push_frame = t_debug_hsv_filter_frame1;
	d.sinks[2].push_frame = t_debug_hsv_filter_frame2;
	d.sinks[3].push_frame = t_debug_hsv_filter_frame3;

	struct xrt_frame_sink *sinks[4] = {
	    &d.sinks[0],
	    &d.sinks[1],
	    &d.sinks[2],
	    &d.sinks[3],
	};

	t_hsv_filter_params params = T_HSV_DEFAULT_PARAMS();
	t_hsv_filter_create(xfctx, &params, sinks, &d.sink);

	xrt_frame_context_add(xfctx, &d.node);

	*out_sink = &d.base;

	return 0;
}
