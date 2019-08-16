// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  @ref xrt_fs_sink converters and other helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_misc.h"
#include "util/u_sink.h"
#include "util/u_format.h"

#include <stdio.h>

#ifdef XRT_HAVE_JPEG
#include "jpeglib.h"
#endif


/*
 *
 * Structs
 *
 */

struct u_sink_converter
{
	struct xrt_fs_sink base;

	struct xrt_fs_sink *downstream;

	uint8_t *data;

	size_t size;
	size_t stride;
	uint32_t width;
	uint32_t height;

	enum xrt_format format;
};


/*
 *
 * YUV functions.
 *
 */

static inline int
clamp_to_byte(int v)
{
	if (v < 0) {
		return 0;
	} else if (v >= 255) {
		return 255;
	} else {
		return v;
	}
}

static inline uint32_t
YUV444_to_RGBX8888(int y, int u, int v)
{
	int C = y - 16;
	int D = u - 128;
	int E = v - 128;

	int R = clamp_to_byte((298 * C + 409 * E + 128) >> 8);
	int G = clamp_to_byte((298 * C - 100 * D - 209 * E + 128) >> 8);
	int B = clamp_to_byte((298 * C + 516 * D + 128) >> 8);

	return B << 16 | G << 8 | R;
}

static uint32_t lookup_YUV_to_RGBX[256][256][256] = {0};

static void
generate_lookup_YUV_to_RGBX()
{
	if (lookup_YUV_to_RGBX[255][255][255] != 0) {
		return;
	}

	int y;
	int u;
	int v;

	for (y = 0; y < 256; y++) {
		for (u = 0; u < 256; u++) {
			for (v = 0; v < 256; v++) {
				lookup_YUV_to_RGBX[y][u][v] =
				    YUV444_to_RGBX8888(y, u, v);
			}
		}
	}
}

inline static void
YUV422_to_R8G8B8X8(const uint8_t *input, uint32_t *rgb1, uint32_t *rgb2)
{
	uint8_t y0 = input[0];
	uint8_t u = input[1];
	uint8_t y1 = input[2];
	uint8_t v = input[3];

	*rgb1 = lookup_YUV_to_RGBX[y0][u][v];
	*rgb2 = lookup_YUV_to_RGBX[y1][u][v];
}

inline static void
YUV422_to_R8G8B8(const uint8_t *input, uint8_t *dst)
{
	uint8_t y0 = input[0];
	uint8_t u = input[1];
	uint8_t y1 = input[2];
	uint8_t v = input[3];

	uint8_t *rgb1 = (uint8_t *)&lookup_YUV_to_RGBX[y0][u][v];
	uint8_t *rgb2 = (uint8_t *)&lookup_YUV_to_RGBX[y1][u][v];

	dst[0] = rgb1[0];
	dst[1] = rgb1[1];
	dst[2] = rgb1[2];
	dst[3] = rgb2[0];
	dst[4] = rgb2[1];
	dst[5] = rgb2[2];
}

static void
from_YUV422_to_R8G8B8(struct u_sink_converter *s,
                      size_t stride,
                      const uint8_t *data)
{
	for (uint32_t y = 0; y < s->height; y++) {
		for (uint32_t x = 0; x < s->width; x += 2) {
			const uint8_t *src = data;
			uint8_t *dst = s->data;

			src = src + (y * stride) + (x * 2);
			dst = dst + (y * s->stride) + (x * 3);
			YUV422_to_R8G8B8(src, dst);
		}
	}
}


/*
 *
 * MJPEG
 *
 */

#ifdef XRT_HAVE_JPEG
static void
from_MJPEG_to_R8G8B8(struct u_sink_converter *s,
                     size_t size,
                     const uint8_t *data)
{

	struct jpeg_decompress_struct cinfo = {0};
	struct jpeg_error_mgr jerr = {0};

	cinfo.err = jpeg_std_error(&jerr);
	jerr.trace_level = 0;

	jpeg_create_decompress(&cinfo);

	jpeg_mem_src(&cinfo, data, size);
	jpeg_read_header(&cinfo, TRUE);

	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&cinfo);

	uint8_t *moving_ptr = s->data;

	uint32_t scanlines_read = 0;
	while (scanlines_read < cinfo.image_height) {
		int read_count = jpeg_read_scanlines(&cinfo, &moving_ptr, 16);
		moving_ptr += read_count * s->stride;
		scanlines_read += read_count;
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
}
#endif


/*
 *
 * Misc functions.
 *
 */

static void
ensure_data(struct u_sink_converter *s, uint32_t w, uint32_t h)
{
	if (s->data != NULL && s->width == w && s->height == h) {
		return;
	}

	s->width = w;
	s->height = h;
	u_format_size_for_dimensions(s->format, s->width, s->height, &s->stride,
	                             &s->size);

	s->data = (uint8_t *)realloc(s->data, s->size);
}

static void
push_frame(struct xrt_fs_sink *xs, struct xrt_fs_frame *xf)
{
	struct u_sink_converter *s = (struct u_sink_converter *)xs;
	s->size = 0;

	if (xf->format == s->format) {
		s->downstream->push_frame(s->downstream, xf);
	}

	ensure_data(s, xf->width, xf->height);

	switch (xf->format) {
	case XRT_FORMAT_YUV422:
		from_YUV422_to_R8G8B8(s, xf->stride, xf->data);
		break;
#ifdef XRT_HAVE_JPEG
	case XRT_FORMAT_MJPEG:
		from_MJPEG_to_R8G8B8(s, xf->size, xf->data);
		break;
#endif
	default:
		fprintf(stderr, "error: Can not convert from '%s'\n",
		        u_format_str(xf->format));
	}

	struct xrt_fs_frame nf = {0};
	nf.width = s->width;
	nf.height = s->height;
	nf.stride = s->stride;
	nf.format = s->format;
	nf.size = s->size;
	nf.data = s->data;

	// Copy directly from original frame.
	nf.timestamp = xf->timestamp;
	nf.source_timestamp = xf->source_timestamp;
	nf.source_sequence = xf->source_sequence;
	nf.source_id = xf->source_id;

	s->downstream->push_frame(s->downstream, &nf);
}


/*
 *
 * "Exported" functions.
 *
 */

void
u_sink_create_format_converter(enum xrt_format f,
                               struct xrt_fs_sink *downstream,
                               struct xrt_fs_sink **out_xfs)
{
	if (f != XRT_FORMAT_R8G8B8) {
		fprintf(stderr, "error: Format '%s' not supported\n",
		        u_format_str(f));
		return;
	}

	generate_lookup_YUV_to_RGBX();

	struct u_sink_converter *s = U_TYPED_CALLOC(struct u_sink_converter);
	s->base.push_frame = push_frame;
	s->downstream = downstream;
	s->format = f;
	*out_xfs = &s->base;
}
