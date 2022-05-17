// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  @ref xrt_frame helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_misc.h"
#include "util/u_frame.h"
#include "util/u_format.h"

#include <assert.h>


static void
free_one_off(struct xrt_frame *xf)
{
	assert(xf->reference.count == 0);
	free(xf->data);
	free(xf);
}

void
u_frame_create_one_off(enum xrt_format f, uint32_t width, uint32_t height, struct xrt_frame **out_frame)
{
	assert(width > 0);
	assert(height > 0);
	assert(u_format_is_blocks(f));

	struct xrt_frame *xf = U_TYPED_CALLOC(struct xrt_frame);

	xf->format = f;
	xf->width = width;
	xf->height = height;
	xf->destroy = free_one_off;

	u_format_size_for_dimensions(f, width, height, &xf->stride, &xf->size);

	xf->data = (uint8_t *)realloc(xf->data, xf->size);

	xrt_frame_reference(out_frame, xf);
}

static void
free_clone(struct xrt_frame *xf)
{
	assert(xf->reference.count == 0);
	free(xf->data);
	free(xf);
}

void
u_frame_clone(struct xrt_frame *to_copy, struct xrt_frame **out_frame)
{
	struct xrt_frame *xf = U_TYPED_CALLOC(struct xrt_frame);

	// Explicitly only copy the fields we want
	xf->width = to_copy->width;
	xf->height = to_copy->height;
	xf->stride = to_copy->stride;
	xf->size = to_copy->size;

	xf->format = to_copy->format;
	xf->stereo_format = to_copy->stereo_format;

	xf->timestamp = to_copy->timestamp;
	xf->source_timestamp = to_copy->source_timestamp;
	xf->source_sequence = to_copy->source_sequence;
	xf->source_id = to_copy->source_id;

	xf->destroy = free_clone;

	xf->data = malloc(xf->size);

	memcpy(xf->data, to_copy->data, xf->size);

	xrt_frame_reference(out_frame, xf);
}

static void
free_roi(struct xrt_frame *xf)
{
	xrt_frame_reference((struct xrt_frame **)&xf->owner, NULL);
	free(xf);
}

void
u_frame_create_roi(struct xrt_frame *original, struct xrt_rect roi, struct xrt_frame **out_frame)
{
	assert(roi.offset.w >= 0 && roi.offset.h >= 0 && roi.extent.w > 0 && roi.extent.h > 0);
	uint32_t x = roi.offset.w;
	uint32_t y = roi.offset.h;
	uint32_t w = roi.extent.w;
	uint32_t h = roi.extent.h;
	assert(x + w <= original->width && y + h <= original->height);

	// Calculate size and offset in bytes

	// Block dimensions
	uint32_t bw = u_format_block_width(original->format);
	uint32_t bh = u_format_block_height(original->format);
	size_t bsz = u_format_block_size(original->format);

	// Only allow x and w to be multiples of bw (same with y, h, and bh)
	assert(w % bw == 0 && x % bw == 0 && h % bh == 0 && y % bh == 0);

	// x, y, w, and h in blocks
	uint32_t xb = x / bw;
	uint32_t yb = y / bh;
	uint32_t wb = w / bw;
	uint32_t hb = h / bh;

	// Compute offset in bytes
	size_t offset = yb * original->stride + xb * bsz;

	// Compute exact size in original to hold the entire ROI
	size_t start_margin = xb * bsz;
	size_t end_margin = original->stride - ((xb + wb) * bsz);
	size_t size = hb * original->stride - start_margin - end_margin;

	// Create and fill in ROI frame

	struct xrt_frame *xf = U_TYPED_CALLOC(struct xrt_frame);

	xf->destroy = free_roi;
	xrt_frame_reference((struct xrt_frame **)&xf->owner, original);

	xf->width = w;
	xf->height = h;
	xf->stride = original->stride;
	xf->size = size;
	xf->data = original->data + offset;

	xf->format = original->format;
	xf->stereo_format = XRT_STEREO_FORMAT_NONE; // Explicitly not-stereo

	xf->timestamp = original->timestamp;
	xf->source_timestamp = original->source_timestamp;
	xf->source_sequence = original->source_sequence;
	xf->source_id = original->source_id;

	xrt_frame_reference(out_frame, xf);
}
