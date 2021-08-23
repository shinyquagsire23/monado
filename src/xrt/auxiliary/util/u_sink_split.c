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


/*!
 * An @ref xrt_frame_sink splitter.
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct u_sink_split
{
	struct xrt_frame_sink base;
	struct xrt_frame_node node;

	struct xrt_frame_sink *left;
	struct xrt_frame_sink *right;
};

static void
split_frame(struct xrt_frame_sink *xfs, struct xrt_frame *xf)
{
	SINK_TRACE_MARKER();

	struct u_sink_split *s = (struct u_sink_split *)xfs;

	s->left->push_frame(s->left, xf);
	s->right->push_frame(s->right, xf);
}

static void
split_break_apart(struct xrt_frame_node *node)
{
	// Noop
}

static void
split_destroy(struct xrt_frame_node *node)
{
	struct u_sink_split *s = container_of(node, struct u_sink_split, node);

	free(s);
}


/*
 *
 * Exported functions.
 *
 */

void
u_sink_split_create(struct xrt_frame_context *xfctx,
                    struct xrt_frame_sink *left,
                    struct xrt_frame_sink *right,
                    struct xrt_frame_sink **out_xfs)
{
	struct u_sink_split *s = U_TYPED_CALLOC(struct u_sink_split);

	s->base.push_frame = split_frame;
	s->node.break_apart = split_break_apart;
	s->node.destroy = split_destroy;
	s->left = left;
	s->right = right;

	xrt_frame_context_add(xfctx, &s->node);

	*out_xfs = &s->base;
}
