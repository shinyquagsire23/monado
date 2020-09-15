// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Format helpers and block code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_format.h"

#include <assert.h>


const char *
u_format_str(enum xrt_format f)
{
	switch (f) {
	case XRT_FORMAT_R8G8B8X8: return "XRT_FORMAT_R8G8B8X8";
	case XRT_FORMAT_R8G8B8A8: return "XRT_FORMAT_R8G8B8A8";
	case XRT_FORMAT_R8G8B8: return "XRT_FORMAT_R8G8B8";
	case XRT_FORMAT_R8G8: return "XRT_FORMAT_R8G8";
	case XRT_FORMAT_R8: return "XRT_FORMAT_R8";
	case XRT_FORMAT_BAYER_GR8: return "XRT_FORMAT_BAYER_GR8";
	case XRT_FORMAT_L8: return "XRT_FORMAT_L8";
	case XRT_FORMAT_BITMAP_8X1: return "XRT_FORMAT_BITMAP_8X1";
	case XRT_FORMAT_BITMAP_8X8: return "XRT_FORMAT_BITMAP_8X8";
	case XRT_FORMAT_YUV888: return "XRT_FORMAT_YUV888";
	case XRT_FORMAT_YUYV422: return "XRT_FORMAT_YUYV422";
	case XRT_FORMAT_UYVY422: return "XRT_FORMAT_UYVY422";
	case XRT_FORMAT_MJPEG: return "XRT_FORMAT_MJPEG";
	default: assert(!"unsupported format"); return 0;
	}
}

bool
u_format_is_blocks(enum xrt_format f)
{
	switch (f) {
	case XRT_FORMAT_R8G8B8X8:
	case XRT_FORMAT_R8G8B8A8:
	case XRT_FORMAT_R8G8B8:
	case XRT_FORMAT_R8G8:
	case XRT_FORMAT_R8:
	case XRT_FORMAT_BAYER_GR8:
	case XRT_FORMAT_L8:
	case XRT_FORMAT_BITMAP_8X1:
	case XRT_FORMAT_BITMAP_8X8:
	case XRT_FORMAT_YUV888:
	case XRT_FORMAT_YUYV422:
	case XRT_FORMAT_UYVY422:
		// Yes
		return true;
	case XRT_FORMAT_MJPEG:
		// Compressed
		return false;
	default: assert(!"unsupported format"); return 0;
	}
}

uint32_t
u_format_block_width(enum xrt_format f)
{
	switch (f) {
	case XRT_FORMAT_R8G8B8X8:
	case XRT_FORMAT_R8G8B8A8:
	case XRT_FORMAT_R8G8B8:
	case XRT_FORMAT_R8G8:
	case XRT_FORMAT_R8:
	case XRT_FORMAT_BAYER_GR8:
	case XRT_FORMAT_L8:
	case XRT_FORMAT_YUV888:
		// Regular one pixel per block formats.
		return 1;
	case XRT_FORMAT_YUYV422:
	case XRT_FORMAT_UYVY422:
		// Two pixels per block.
		return 2;
	case XRT_FORMAT_BITMAP_8X8:
	case XRT_FORMAT_BITMAP_8X1:
		// Eight pixels per block.
		return 8; // NOLINT
	default: assert(!"unsupported format"); return 0;
	}
}

uint32_t
u_format_block_height(enum xrt_format f)
{
	switch (f) {
	case XRT_FORMAT_R8G8B8X8:
	case XRT_FORMAT_R8G8B8A8:
	case XRT_FORMAT_R8G8B8:
	case XRT_FORMAT_R8G8:
	case XRT_FORMAT_R8:
	case XRT_FORMAT_BAYER_GR8:
	case XRT_FORMAT_L8:
	case XRT_FORMAT_BITMAP_8X1:
	case XRT_FORMAT_YUV888:
	case XRT_FORMAT_YUYV422:
	case XRT_FORMAT_UYVY422:
		// One pixel high.
		return 1;
	case XRT_FORMAT_BITMAP_8X8:
		// Eight pixels high.
		return 8;
	default: assert(!"unsupported format"); return 0;
	}
}

size_t
u_format_block_size(enum xrt_format f)
{
	switch (f) {
	case XRT_FORMAT_BITMAP_8X1:
	case XRT_FORMAT_R8:
	case XRT_FORMAT_BAYER_GR8:
	case XRT_FORMAT_L8:
		// One byte blocks
		return 1;
	case XRT_FORMAT_R8G8:
		// Two bytes, 16bits.
		return 2;
	case XRT_FORMAT_R8G8B8:
	case XRT_FORMAT_YUV888:
		// Weird 24bit pixel formats.
		return 3;
	case XRT_FORMAT_R8G8B8X8:
	case XRT_FORMAT_R8G8B8A8:
	case XRT_FORMAT_YUYV422: // Four bytes per two pixels.
	case XRT_FORMAT_UYVY422: // Four bytes per two pixels.
		// 32bit pixel formats.
		return 4;
	case XRT_FORMAT_BITMAP_8X8: // 64 bits.
		return 8;
	default: assert(!"unsupported format"); return 0;
	}
}

void
u_format_size_for_dimensions(enum xrt_format f, uint32_t width, uint32_t height, size_t *out_stride, size_t *out_size)
{
	uint32_t sw = u_format_block_width(f);
	uint32_t sh = u_format_block_height(f);
	size_t block_size = u_format_block_size(f);

	// Round up
	uint32_t num_blocks_x = (width + (sw - 1)) / sw;
	uint32_t num_blocks_y = (height + (sh - 1)) / sh;

	// Add it all together
	size_t stride = num_blocks_x * block_size;
	size_t size = num_blocks_y * stride;

	*out_stride = stride;
	*out_size = size;
}
