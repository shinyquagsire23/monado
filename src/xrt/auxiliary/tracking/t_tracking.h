// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tracking API interface.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#pragma once

#include "xrt/xrt_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *
 * Pre-declare
 *
 */

struct xrt_tracked_psmv;
struct xrt_tracked_psvr;


/*
 *
 * Conversion functions.
 *
 */

struct t_convert_table
{
	uint8_t v[256][256][256][3];
};

void
t_convert_fill_table(struct t_convert_table *t);

void
t_convert_make_y8u8v8_to_r8g8b8(struct t_convert_table *t);

void
t_convert_make_y8u8v8_to_h8s8v8(struct t_convert_table *t);

void
t_convert_make_h8s8v8_to_r8g8b8(struct t_convert_table *t);

void
t_convert_in_place_y8u8v8_to_r8g8b8(uint32_t width,
                                    uint32_t height,
                                    size_t stride,
                                    void *data_ptr);

void
t_convert_in_place_y8u8v8_to_h8s8v8(uint32_t width,
                                    uint32_t height,
                                    size_t stride,
                                    void *data_ptr);

void
t_convert_in_place_h8s8v8_to_r8g8b8(uint32_t width,
                                    uint32_t height,
                                    size_t stride,
                                    void *data_ptr);


/*
 *
 * Filter functions.
 *
 */

#define T_HSV_SIZE 32
#define T_HSV_STEP (256 / T_HSV_SIZE)

#define T_HSV_DEFAULT_PARAMS()                                                 \
	{                                                                      \
		{                                                              \
		    {165, 30, 160, 100},                                       \
		    {135, 30, 160, 100},                                       \
		    {95, 30, 160, 100},                                        \
		},                                                             \
		    {128, 80},                                                 \
	}

struct t_hsv_filter_color
{
	uint8_t hue_min;
	uint8_t hue_range;

	uint8_t s_min;

	uint8_t v_min;
};

struct t_hsv_filter_params
{
	struct t_hsv_filter_color color[3];

	struct
	{
		uint8_t s_max;
		uint8_t v_min;
	} white;
};

struct t_hsv_filter_large_table
{
	uint8_t v[256][256][256];
};

struct t_hsv_filter_optimized_table
{
	uint8_t v[T_HSV_SIZE][T_HSV_SIZE][T_HSV_SIZE];
};

void
t_hsv_build_convert_table(struct t_hsv_filter_params *params,
                          struct t_convert_table *t);

void
t_hsv_build_large_table(struct t_hsv_filter_params *params,
                        struct t_hsv_filter_large_table *t);

void
t_hsv_build_optimized_table(struct t_hsv_filter_params *params,
                            struct t_hsv_filter_optimized_table *t);

XRT_MAYBE_UNUSED static inline uint8_t
t_hsv_filter_sample(struct t_hsv_filter_optimized_table *t,
                    uint32_t y,
                    uint32_t u,
                    uint32_t v)
{
	return t->v[y / T_HSV_STEP][u / T_HSV_STEP][v / T_HSV_STEP];
}

int
t_hsv_filter_create(struct xrt_frame_context *xfctx,
                    struct t_hsv_filter_params *params,
                    struct xrt_frame_sink *sinks[4],
                    struct xrt_frame_sink **out_sink);


/*
 *
 * Tracker code.
 *
 */

int
t_psmv_start(struct xrt_tracked_psmv *xtmv);

int
t_psmv_create(struct xrt_frame_context *xfctx,
              struct xrt_colour_rgb_f32 *rgb,
              struct xrt_tracked_psmv **out_xtmv,
              struct xrt_frame_sink **out_sink);

int
t_psvr_start(struct xrt_tracked_psvr *xtvr);

int
t_psvr_create(struct xrt_frame_context *xfctx,
              struct xrt_tracked_psvr **out_xtvr,
              struct xrt_frame_sink **out_sink);

/*
 *
 * Camera calibration
 *
 */


#define T_CALIBRATION_DEFAULT_PARAMS                                           \
	{                                                                      \
		9, 7, 0.025f,                                                  \
	}

struct t_calibration_params
{
	int checker_cols_num;
	int checker_rows_num;
	float checker_size_meters;
};

int
t_calibration_stereo_create(struct xrt_frame_context *xfctx,
                            struct t_calibration_params *params,
                            struct xrt_frame_sink *gui,
                            struct xrt_frame_sink **out_sink);


/*
 *
 * Sink creation functions.
 *
 */

int
t_convert_yuv_or_yuyv_create(struct xrt_frame_sink *next,
                             struct xrt_frame_sink **out_sink);



int
t_debug_hsv_picker_create(struct xrt_frame_context *xfctx,
                          struct xrt_frame_sink *passthrough,
                          struct xrt_frame_sink **out_sink);

int
t_debug_hsv_viewer_create(struct xrt_frame_context *xfctx,
                          struct xrt_frame_sink *passthrough,
                          struct xrt_frame_sink **out_sink);

int
t_debug_hsv_filter_create(struct xrt_frame_context *xfctx,
                          struct xrt_frame_sink *passthrough,
                          struct xrt_frame_sink **out_sink);


#ifdef __cplusplus
}
#endif
