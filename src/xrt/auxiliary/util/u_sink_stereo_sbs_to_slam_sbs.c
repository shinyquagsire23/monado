// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  An @ref xrt_frame_sink splitter.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_misc.h"
#include "util/u_sink.h"
#include "util/u_trace_marker.h"
#include "util/u_frame.h"
#include "xrt/xrt_frame.h"

//!@todo Extend this to over-and-under frames!

/*!
 * An @ref xrt_frame_sink splitter.
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct u_sink_stereo_sbs_to_slam_sbs
{
	struct xrt_frame_sink base;
	struct xrt_frame_node node;

	struct xrt_frame_sink *downstream_left;
	struct xrt_frame_sink *downstream_right;
};

static void
split_frame(struct xrt_frame_sink *xfs, struct xrt_frame *xf)
{
	SINK_TRACE_MARKER();

	struct u_sink_stereo_sbs_to_slam_sbs *s = (struct u_sink_stereo_sbs_to_slam_sbs *)xfs;

	assert(xf->width % 2 == 0);

	int one_frame_width = xf->width / 2;

	struct xrt_rect left;
	struct xrt_rect right;

	left.offset.h = 0;
	left.offset.w = 0;
	left.extent.h = xf->height;
	left.extent.w = one_frame_width;

	right.offset.h = 0;
	right.offset.w = one_frame_width;
	right.extent.h = xf->height;
	right.extent.w = one_frame_width;
	struct xrt_frame *xf_left = NULL;
	struct xrt_frame *xf_right = NULL;
	u_frame_create_roi(xf, left, &xf_left);
	u_frame_create_roi(xf, right, &xf_right);

	xrt_sink_push_frame(s->downstream_left, xf_left);
	xrt_sink_push_frame(s->downstream_right, xf_right);

	xrt_frame_reference(&xf_left, NULL);
	xrt_frame_reference(&xf_right, NULL);
}

static void
split_break_apart(struct xrt_frame_node *node)
{
	// Noop
}

static void
split_destroy(struct xrt_frame_node *node)
{
	struct u_sink_stereo_sbs_to_slam_sbs *s = container_of(node, struct u_sink_stereo_sbs_to_slam_sbs, node);

	free(s);
}


/*
 *
 * Exported functions.
 *
 */

void
u_sink_stereo_sbs_to_slam_sbs_create(struct xrt_frame_context *xfctx,
                                     struct xrt_frame_sink *downstream_left,
                                     struct xrt_frame_sink *downstream_right,
                                     struct xrt_frame_sink **out_xfs)
{
	struct u_sink_stereo_sbs_to_slam_sbs *s = U_TYPED_CALLOC(struct u_sink_stereo_sbs_to_slam_sbs);

	s->base.push_frame = split_frame;
	s->node.break_apart = split_break_apart;
	s->node.destroy = split_destroy;
	s->downstream_left = downstream_left;
	s->downstream_right = downstream_right;

	xrt_frame_context_add(xfctx, &s->node);

	*out_xfs = &s->base;
}
