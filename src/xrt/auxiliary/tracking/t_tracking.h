// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tracking API interface.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#pragma once

#include "xrt/xrt_frame.h"

#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup aux_tracking Tracking
 * @ingroup aux
 * @brief Trackers, filters and associated helper code.
 *
 *
 * ### Coordinate system
 *
 * Right now there is no specific convention on where a tracking systems
 * coordinate system is centered, and is something we probably need to figure
 * out. Right now the stereo based tracking system used by the PSVR and PSMV
 * tracking system is centered on the camera that OpenCV decided is origin.
 *
 * To go a bit further on the PSVR/PSMV case. Think about a idealized start up
 * case, the user is wearing the HMD headset and holding two PSMV controllers.
 * The HMD's coordinate system axis are perfectly parallel with the user
 * coordinate with the user's coordinate system. Where -Z is forward. The user
 * holds the controllers with the ball pointing up and the buttons on the back
 * pointing forward. Which if you read the documentation of @ref psmv_device
 * will that the axis of the PSMV are also perfectly aligned with the users
 * coordinate system. So everything "attached" to the user have it's coordinate
 * system parallel to the user's.
 *
 * The camera on the other hand is looking directly at the user, it's Z-axis and
 * X-axis is flipped in relation to the user's. So to compare what is sees to
 * what the user sees, everything is rotated 180° around the Y-axis.
 */

/*!
 * @dir auxiliary/tracking
 * @ingroup aux
 *
 * @brief Trackers, filters and associated helper code.
 */

/*!
 * @ingroup aux_tracking
 * @{
 */


/*
 *
 * Pre-declare
 *
 */

struct xrt_tracked_psmv;
struct xrt_tracked_psvr;


/*
 *
 * Calibration data.
 *
 */

/*!
 * Refined calibration data settings to be given to trackers.
 */
struct t_settings_stereo
{
	//! Source image size before undistortion.
	struct xrt_size image_size_pixels;
	//! Target image size after undistortion.
	struct xrt_size new_image_size_pixels;

	//! Disparity and position to camera world coordinates.
	double disparity_to_depth[4][4];
};

/*!
 * Raw calibration data settings saved and loaded from disk.
 */
struct t_settings_stereo_raw
{
	//! Source image size before undistortion.
	struct xrt_size image_size_pixels;
	//! Target image size after undistortion.
	struct xrt_size new_image_size_pixels;

	//! Translation between the two cameras, in the stereo pair.
	double camera_translation[3];
	//! Rotation matrix between the two cameras, in the stereo pair.
	double camera_rotation[3][3];
	//! Essential matrix.
	double camera_essential[3][3];
	//! Fundamental matrix.
	double camera_fundamental[3][3];

	//! Disparity and position to camera world coordinates.
	double disparity_to_depth[4][4];

	//! Left camera intrinsics
	double l_intrinsics[3][3];
	//! Left camera distortion
	double l_distortion[5];
	//! Left fisheye camera distortion
	double l_distortion_fisheye[4];
	//! Left rectification transform (rotation matrix).
	double l_rotation[3][3];
	//! Left projection matrix in the new (rectified) coordinate systems.
	double l_projection[3][4];

	//! Right camera intrinsics
	double r_intrinsics[3][3];
	//! Right camera distortion
	double r_distortion[5];
	//! Right fisheye camera distortion
	double r_distortion_fisheye[4];
	//! Right rectification transform (rotation matrix).
	double r_rotation[3][3];
	//! Right projection matrix in the new (rectified) coordinate systems.
	double r_projection[3][4];
};

/*!
 * Refine a raw calibration data, this how you create stereo calibration data.
 */
void
t_settings_stereo_refine(struct t_settings_stereo_raw *raw_data,
                         struct t_settings_stereo **out_data);

/*!
 * Free stereo calibration data.
 */
void
t_settings_stereo_free(struct t_settings_stereo **data_ptr);

/*!
 * Create a raw stereo calibration data.
 */
void
t_settings_stereo_raw_create(struct t_settings_stereo_raw **out_raw_data);

/*!
 * Free raw stereo calibration data.
 */
void
t_settings_stereo_raw_free(struct t_settings_stereo_raw **raw_data_ptr);

/*!
 * Load stereo calibration data from a given file.
 */
bool
t_settings_stereo_load_v1(FILE *calib_file,
                          struct t_settings_stereo_raw **out_raw_data);

/*!
 * Load a stereo calibration struct from a hardcoded place.
 */
bool
t_settings_stereo_load_v1_hack(struct t_settings_stereo_raw **out_raw_data);


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
              struct t_settings_stereo *data,
              struct xrt_tracked_psmv **out_xtmv,
              struct xrt_frame_sink **out_sink);

int
t_psvr_start(struct xrt_tracked_psvr *xtvr);

int
t_psvr_create(struct xrt_frame_context *xfctx,
              struct t_settings_stereo *data,
              struct xrt_tracked_psvr **out_xtvr,
              struct xrt_frame_sink **out_sink);


/*
 *
 * Camera calibration
 *
 */

#define T_CALIBRATION_DEFAULT_PARAMS                                           \
	{                                                                      \
		9, 7, 0.0173333f, true, 5, 20, 60, 2                           \
	}

struct t_calibration_params
{
	int checker_cols_num;
	int checker_rows_num;
	float checker_size_meters;

	bool subpixel_enable;
	int subpixel_size;

	uint32_t num_wait_for;
	uint32_t num_collect_total;
	uint32_t num_collect_restart;
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

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
