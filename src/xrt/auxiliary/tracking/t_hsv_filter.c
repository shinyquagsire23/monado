// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A simple HSV filter.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_sink.h"
#include "util/u_debug.h"
#include "util/u_frame.h"
#include "util/u_format.h"
#include "util/u_trace_marker.h"

#include "tracking/t_tracking.h"

#include <stdio.h>
#include <assert.h>


#define MOD_180(v) ((uint32_t)(v) % 180)

static inline bool
check_range(struct t_hsv_filter_color color, uint32_t h, uint32_t s, uint32_t v)
{
	bool bad = false;
	bad |= s < color.s_min;
	bad |= v < color.v_min;
	bad |= MOD_180(h + (360 - color.hue_min)) >= color.hue_range;
	return !bad;
}

void
t_hsv_build_convert_table(struct t_hsv_filter_params *params, struct t_convert_table *t)
{
	struct t_hsv_filter_large_table *temp = U_TYPED_CALLOC(struct t_hsv_filter_large_table);
	t_hsv_build_large_table(params, temp);

	uint8_t *dst = &t->v[0][0][0][0];

	for (int y = 0; y < 256; y++) {
		for (int u = 0; u < 256; u++) {
			for (int v = 0; v < 256; v++) {
				uint32_t mask = temp->v[y][u][v];

				dst[0] = ((mask & 1) != 0) ? 0xff : 0x00;
				dst[1] = ((mask & 2) != 0) ? 0xff : 0x00;
				dst[2] = ((mask & 4) != 0) ? 0xff : 0x00;

				dst += 3;
			}
		}
	}

	free(temp);
}

void
t_hsv_build_large_table(struct t_hsv_filter_params *params, struct t_hsv_filter_large_table *t)
{
	struct t_convert_table *temp = U_TYPED_CALLOC(struct t_convert_table);
	t_convert_make_y8u8v8_to_h8s8v8(temp);

	uint8_t *dst = &t->v[0][0][0];
	for (int y = 0; y < 256; y++) {
		for (int u = 0; u < 256; u++) {
			for (int v = 0; v < 256; v++) {
				uint32_t h = temp->v[y][u][v][0];
				uint8_t s = temp->v[y][u][v][1];
				uint8_t v2 = temp->v[y][u][v][2];

				bool f0 = check_range(params->color[0], h, s, v2);
				bool f1 = check_range(params->color[1], h, s, v2);
				bool f2 = check_range(params->color[2], h, s, v2);
				bool f3 = s <= params->white.s_max && v2 >= params->white.v_min;

				*dst = (f0 << 0) | (f1 << 1) | (f2 << 2) | (f3 << 3);
				dst += 1;
			}
		}
	}

	free(temp);
}

void
t_hsv_build_optimized_table(struct t_hsv_filter_params *params, struct t_hsv_filter_optimized_table *t)
{
	struct t_hsv_filter_large_table *temp = U_TYPED_CALLOC(struct t_hsv_filter_large_table);
	t_hsv_build_large_table(params, temp);

	// Half of step, minus one
	int offset = (T_HSV_STEP / 2) - 1;

	for (int y = 0; y < T_HSV_SIZE; y++) {

		int src_y = y * T_HSV_STEP + offset;

		for (int u = 0; u < T_HSV_SIZE; u++) {

			int src_u = u * T_HSV_STEP + offset;
			int src_v = offset;

			for (int v = 0; v < T_HSV_SIZE; v++) {
				t->v[y][u][v] = temp->v[src_y][src_u][src_v];

				src_v += T_HSV_STEP;
			}
		}
	}

	free(temp);
}


/*
 *
 * Sink filter
 *
 */

#define NUM_CHANNELS 4

/*!
 * An @ref xrt_frame_sink that splits the input based on hue.
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct t_hsv_filter
{
	struct xrt_frame_sink base;
	struct xrt_frame_node node;


	struct xrt_frame_sink *sinks[NUM_CHANNELS];

	struct t_hsv_filter_params params;

	struct xrt_frame *frames[NUM_CHANNELS];

	struct u_sink_debug usds[NUM_CHANNELS];

	struct t_hsv_filter_optimized_table table;
};

static void
process_sample(struct t_hsv_filter *f,
               uint8_t y,
               uint8_t cb,
               uint8_t cr,
               uint8_t *dst0,
               uint8_t *dst1,
               uint8_t *dst2,
               uint8_t *dst3)
{
	uint8_t bits = t_hsv_filter_sample(&f->table, y, cb, cr);
	uint8_t v0 = (bits & (1 << 0)) ? 0xff : 0x00;
	uint8_t v1 = (bits & (1 << 1)) ? 0xff : 0x00;
	uint8_t v2 = (bits & (1 << 2)) ? 0xff : 0x00;
	uint8_t v3 = (bits & (1 << 3)) ? 0xff : 0x00;

	*dst0 = v0;
	*dst1 = v1;
	*dst2 = v2;
	*dst3 = v3;
}

XRT_NO_INLINE static void
hsv_process_frame_yuv(struct t_hsv_filter *f, struct xrt_frame *xf)
{
	SINK_TRACE_MARKER();

	struct xrt_frame *f0 = f->frames[0];
	struct xrt_frame *f1 = f->frames[1];
	struct xrt_frame *f2 = f->frames[2];
	struct xrt_frame *f3 = f->frames[3];

	for (uint32_t y = 0; y < xf->height; y++) {
		uint8_t *src = (uint8_t *)xf->data + y * xf->stride;
		uint8_t *dst0 = f0->data + y * f0->stride;
		uint8_t *dst1 = f1->data + y * f1->stride;
		uint8_t *dst2 = f2->data + y * f2->stride;
		uint8_t *dst3 = f3->data + y * f3->stride;

		for (uint32_t x = 0; x < xf->width; x += 1) {
			uint8_t y = src[0];
			uint8_t cb = src[1];
			uint8_t cr = src[2];
			src += 3;

			process_sample(f, y, cb, cr, dst0, dst1, dst2, dst3);
			dst0 += 1;
			dst1 += 1;
			dst2 += 1;
			dst3 += 1;
		}
	}
}

XRT_NO_INLINE static void
hsv_process_frame_yuyv(struct t_hsv_filter *f, struct xrt_frame *xf)
{
	SINK_TRACE_MARKER();

	struct xrt_frame *f0 = f->frames[0];
	struct xrt_frame *f1 = f->frames[1];
	struct xrt_frame *f2 = f->frames[2];
	struct xrt_frame *f3 = f->frames[3];

	for (uint32_t y = 0; y < xf->height; y++) {
		uint8_t *src = (uint8_t *)xf->data + y * xf->stride;
		uint8_t *dst0 = f0->data + y * f0->stride;
		uint8_t *dst1 = f1->data + y * f1->stride;
		uint8_t *dst2 = f2->data + y * f2->stride;
		uint8_t *dst3 = f3->data + y * f3->stride;

		for (uint32_t x = 0; x < xf->width; x += 2) {
			uint8_t y1 = src[0];
			uint8_t cb = src[1];
			uint8_t y2 = src[2];
			uint8_t cr = src[3];
			src += 4;

			uint8_t bits0 = t_hsv_filter_sample(&f->table, y1, cb, cr);
			uint8_t bits1 = t_hsv_filter_sample(&f->table, y2, cb, cr);

			uint8_t v0 = (bits0 & (1 << 0)) ? 0xff : 0x00;
			uint8_t v1 = (bits0 & (1 << 1)) ? 0xff : 0x00;
			uint8_t v2 = (bits0 & (1 << 2)) ? 0xff : 0x00;
			uint8_t v3 = (bits0 & (1 << 3)) ? 0xff : 0x00;
			uint8_t v4 = (bits1 & (1 << 0)) ? 0xff : 0x00;
			uint8_t v5 = (bits1 & (1 << 1)) ? 0xff : 0x00;
			uint8_t v6 = (bits1 & (1 << 2)) ? 0xff : 0x00;
			uint8_t v7 = (bits1 & (1 << 3)) ? 0xff : 0x00;

			*(uint16_t *)dst0 = v0 | v4 << 8;
			*(uint16_t *)dst1 = v1 | v5 << 8;
			*(uint16_t *)dst2 = v2 | v6 << 8;
			*(uint16_t *)dst3 = v3 | v7 << 8;

			dst0 += 2;
			dst1 += 2;
			dst2 += 2;
			dst3 += 2;
		}
	}
}

static void
ensure_buf_allocated(struct t_hsv_filter *f, struct xrt_frame *xf)
{
	uint32_t w = xf->width;
	uint32_t h = xf->height;

	for (size_t i = 0; i < NUM_CHANNELS; i++) {
		u_frame_create_one_off(XRT_FORMAT_L8, w, h, &f->frames[i]);
	}
}

static void
push_buf(struct t_hsv_filter *f,
         struct xrt_frame *orig_xf,
         struct xrt_frame_sink *xsink,
         struct u_sink_debug *usd,
         struct xrt_frame *xf)
{
	xf->timestamp = orig_xf->timestamp;
	xf->source_id = orig_xf->source_id;
	xf->stereo_format = orig_xf->stereo_format;
	xf->source_sequence = orig_xf->source_sequence;
	xf->source_timestamp = orig_xf->source_timestamp;

	if (xsink != NULL) {
		xrt_sink_push_frame(xsink, xf);
	}

	u_sink_debug_push_frame(usd, xf);
}

static void
hsv_frame(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	SINK_TRACE_MARKER();

	struct t_hsv_filter *f = (struct t_hsv_filter *)xsink;


	switch (xf->format) {
	case XRT_FORMAT_YUV888:
		ensure_buf_allocated(f, xf);
		hsv_process_frame_yuv(f, xf);
		break;
	case XRT_FORMAT_YUYV422:
		ensure_buf_allocated(f, xf);
		hsv_process_frame_yuyv(f, xf);
		break;
	default: U_LOG_E("Bad format '%s'", u_format_str(xf->format)); return;
	}

	for (size_t i = 0; i < NUM_CHANNELS; i++) {
		push_buf(f, xf, f->sinks[i], &f->usds[i], f->frames[i]);
		xrt_frame_reference(&f->frames[i], NULL);
	}
}

static void
hsv_break_apart(struct xrt_frame_node *node)
{
	// Noop
}

static void
hsv_destroy(struct xrt_frame_node *node)
{
	struct t_hsv_filter *f = container_of(node, struct t_hsv_filter, node);
	u_var_remove_root(f);
	for (size_t i = 0; i < ARRAY_SIZE(f->usds); i++) {
		u_sink_debug_destroy(&f->usds[i]);
	}

	free(f);
}

int
t_hsv_filter_create(struct xrt_frame_context *xfctx,
                    struct t_hsv_filter_params *params,
                    struct xrt_frame_sink *sinks[4],
                    struct xrt_frame_sink **out_sink)
{
	struct t_hsv_filter *f = U_TYPED_CALLOC(struct t_hsv_filter);
	f->base.push_frame = hsv_frame;
	f->node.break_apart = hsv_break_apart;
	f->node.destroy = hsv_destroy;
	f->params = *params;
	f->sinks[0] = sinks[0];
	f->sinks[1] = sinks[1];
	f->sinks[2] = sinks[2];
	f->sinks[3] = sinks[3];

	t_hsv_build_optimized_table(&f->params, &f->table);

	xrt_frame_context_add(xfctx, &f->node);

	for (size_t i = 0; i < NUM_CHANNELS; i++) {
		u_sink_debug_init(&f->usds[i]);
	}
	u_var_add_root(f, "HSV Filter", true);
	u_var_add_sink_debug(f, &f->usds[0], "Red");
	u_var_add_sink_debug(f, &f->usds[1], "Purple");
	u_var_add_sink_debug(f, &f->usds[2], "Blue");
	u_var_add_sink_debug(f, &f->usds[3], "White");

	*out_sink = &f->base;

	return 0;
}
