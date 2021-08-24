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

	// Paranoia: Explicitly only copy the fields we want
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
