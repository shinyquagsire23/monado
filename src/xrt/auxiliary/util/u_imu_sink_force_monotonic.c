// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A @ref xrt_imu_sink that forces the samples to be monotonically increasing.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_sink.h"
#include "util/u_trace_marker.h"
#include "util/u_logging.h"

#include <inttypes.h>

/*!
 * An @ref xrt_imu_sink splitter.
 * @implements xrt_imu_sink
 * @implements xrt_frame_node
 */
struct u_imu_sink_force_monotonic
{
	struct xrt_imu_sink base;
	struct xrt_frame_node node;

	timepoint_ns last_ts;
	struct xrt_imu_sink *downstream;
};

static void
split_sample(struct xrt_imu_sink *xfs, struct xrt_imu_sample *sample)
{
	SINK_TRACE_MARKER();

	struct u_imu_sink_force_monotonic *s = (struct u_imu_sink_force_monotonic *)xfs;

	if (sample->timestamp_ns == s->last_ts) {
		U_LOG_W("Got an IMU sample with a duplicate timestamp! Old: %" PRId64 "; New: %" PRId64 "", s->last_ts,
		        sample->timestamp_ns);
		return;
	}
	if (sample->timestamp_ns < s->last_ts) {
		U_LOG_W("Got an IMU sample with a non-monotonically-increasing timestamp! Old: %" PRId64
		        "; New: %" PRId64 "",
		        s->last_ts, sample->timestamp_ns);
		return;
	}

	s->last_ts = sample->timestamp_ns;

	xrt_sink_push_imu(s->downstream, sample);
}

static void
split_break_apart(struct xrt_frame_node *node)
{
	// Noop
}

static void
split_destroy(struct xrt_frame_node *node)
{
	struct u_imu_sink_force_monotonic *s = container_of(node, struct u_imu_sink_force_monotonic, node);

	free(s);
}

/*
 *
 * Exported functions.
 *
 */

void
u_imu_sink_force_monotonic_create(struct xrt_frame_context *xfctx,
                                  struct xrt_imu_sink *downstream,
                                  struct xrt_imu_sink **out_imu_sink)
{

	struct u_imu_sink_force_monotonic *s = U_TYPED_CALLOC(struct u_imu_sink_force_monotonic);
	s->base.push_imu = split_sample;
	s->node.break_apart = split_break_apart;
	s->node.destroy = split_destroy;
	s->downstream = downstream;

	xrt_frame_context_add(xfctx, &s->node);
	*out_imu_sink = &s->base;
}
