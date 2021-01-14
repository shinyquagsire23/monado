// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  An @ref xrt_frame_sink that deinterleaves stereo frames.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_misc.h"
#include "util/u_sink.h"
#include "util/u_frame.h"


/*!
 * An @ref xrt_frame_sink that deinterleaves stereo frames.
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct u_sink_deinterleaver
{
	struct xrt_frame_sink base;
	struct xrt_frame_node node;

	struct xrt_frame_sink *downstream;
};


/*
 *
 * Helpers
 *
 */

inline static void
L8_interleaved_to_L8(const uint8_t *input, uint8_t *l8a, uint8_t *l8b)
{
	*l8a = input[0];
	*l8b = input[1];
}

static void
from_L8_interleaved_to_L8(struct xrt_frame *frame, uint32_t w, uint32_t h, size_t stride, const uint8_t *data)
{
	uint32_t half_w = w / 2;

	for (uint32_t y = 0; y < h; y++) {
		const uint8_t *src = data + (y * stride);
		uint8_t *dst = frame->data + (y * frame->stride);

		for (uint32_t x = 0; x < half_w; x++) {
			L8_interleaved_to_L8(src, dst, dst + half_w);

			dst += 1;
			src += 2;
		}
	}
}


/*
 *
 * Helpers
 *
 */

static void
deinterleaves_frame(struct xrt_frame_sink *xfs, struct xrt_frame *xf)
{
	struct u_sink_deinterleaver *de = (struct u_sink_deinterleaver *)xfs;

	if (xf->stereo_format != XRT_STEREO_FORMAT_INTERLEAVED) {
		de->downstream->push_frame(de->downstream, xf);
		return;
	}

	if (xf->format != XRT_FORMAT_L8) {
		de->downstream->push_frame(de->downstream, xf);
		return;
	}

	enum xrt_format format = XRT_FORMAT_L8;
	uint32_t w = xf->width;
	uint32_t h = xf->height;
	size_t stride = xf->stride;
	const uint8_t *data = xf->data;
	struct xrt_frame *frame = NULL;

	u_frame_create_one_off(format, w, h, &frame);

	// Copy directly from original frame.
	frame->timestamp = xf->timestamp;
	frame->source_timestamp = xf->source_timestamp;
	frame->source_sequence = xf->source_sequence;
	frame->source_id = xf->source_id;
	frame->stereo_format = XRT_STEREO_FORMAT_SBS;

	// Copy the data.
	from_L8_interleaved_to_L8(frame, w, h, stride, data);

	// Push downstream.
	de->downstream->push_frame(de->downstream, frame);

	// Refcount in case it's being held downstream.
	xrt_frame_reference(&frame, NULL);
}

static void
break_apart(struct xrt_frame_node *node)
{}

static void
destroy(struct xrt_frame_node *node)
{
	struct u_sink_deinterleaver *de = container_of(node, struct u_sink_deinterleaver, node);

	free(de);
}


/*
 *
 * Exported functions.
 *
 */

void
u_sink_deinterleaver_create(struct xrt_frame_context *xfctx,
                            struct xrt_frame_sink *downstream,
                            struct xrt_frame_sink **out_xfs)
{
	struct u_sink_deinterleaver *de = U_TYPED_CALLOC(struct u_sink_deinterleaver);

	de->base.push_frame = deinterleaves_frame;
	de->node.break_apart = break_apart;
	de->node.destroy = destroy;
	de->downstream = downstream;

	*out_xfs = &de->base;
}
