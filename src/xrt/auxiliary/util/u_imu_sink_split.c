// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  An @ref xrt_imu_sink splitter.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_sink.h"
#include "util/u_trace_marker.h"


/*!
 * An @ref xrt_imu_sink splitter.
 * @implements xrt_imu_sink
 * @implements xrt_frame_node
 */
struct u_imu_sink_split
{
	struct xrt_imu_sink base;
	struct xrt_frame_node node;

	struct xrt_imu_sink *downstream_one;
	struct xrt_imu_sink *downstream_two;
};

static void
split_sample(struct xrt_imu_sink *xfs, struct xrt_imu_sample *sample)
{
	SINK_TRACE_MARKER();

	struct u_imu_sink_split *s = (struct u_imu_sink_split *)xfs;

	xrt_sink_push_imu(s->downstream_one, sample);
	xrt_sink_push_imu(s->downstream_two, sample);
}

static void
split_break_apart(struct xrt_frame_node *node)
{
	// Noop
}

static void
split_destroy(struct xrt_frame_node *node)
{
	struct u_imu_sink_split *s = container_of(node, struct u_imu_sink_split, node);

	free(s);
}

/*
 *
 * Exported functions.
 *
 */

void
u_imu_sink_split_create(struct xrt_frame_context *xfctx,
                        struct xrt_imu_sink *downstream_one,
                        struct xrt_imu_sink *downstream_two,
                        struct xrt_imu_sink **out_imu_sink)
{

	struct u_imu_sink_split *s = U_TYPED_CALLOC(struct u_imu_sink_split);
	s->base.push_imu = split_sample;
	s->node.break_apart = split_break_apart;
	s->node.destroy = split_destroy;
	s->downstream_one = downstream_one;
	s->downstream_two = downstream_two;

	xrt_frame_context_add(xfctx, &s->node);
	*out_imu_sink = &s->base;
}
