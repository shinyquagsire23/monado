// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tracking API interface.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_tracking
 */

#pragma once

#include "xrt/xrt_frame.h"
#include "util/u_misc.h"

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
struct xrt_tracked_hand;


/*
 *
 * Calibration data.
 *
 */

//! Maximum size of rectilinear distortion coefficient array
#define XRT_DISTORTION_MAX_DIM (5)

/*!
 * @brief Essential calibration data for a single camera, or single lens/sensor
 * of a stereo camera.
 */
struct t_camera_calibration
{
	//! Source image size
	struct xrt_size image_size_pixels;

	//! Camera intrinsics matrix
	double intrinsics[3][3];

	//! Rectilinear distortion coefficients: k1, k2, p1, p2[, k3[, k4, k5,
	//! k6[, s1, s2, s3, s4]]
	double distortion[XRT_DISTORTION_MAX_DIM];

	//! Fisheye camera distortion coefficients
	double distortion_fisheye[4];

	//! Is the camera fisheye?
	bool use_fisheye;
};

/*!
 * Stereo camera calibration data to be given to trackers.
 */
struct t_stereo_camera_calibration
{
	//! Ref counting
	struct xrt_reference reference;

	//! Calibration of individual views/sensor
	struct t_camera_calibration view[2];

	//! Translation from first to second in the stereo pair.
	double camera_translation[3];
	//! Rotation matrix from first to second in the stereo pair.
	double camera_rotation[3][3];

	//! Essential matrix.
	double camera_essential[3][3];
	//! Fundamental matrix.
	double camera_fundamental[3][3];
};

/*!
 * Allocates a new stereo calibration data, unreferences the old @p calib.
 *
 * @public @memberof t_stereo_camera_calibration
 */
void
t_stereo_camera_calibration_alloc(struct t_stereo_camera_calibration **calib);

/*!
 * Only to be called by @p t_stereo_camera_calibration_reference.
 *
 * @private @memberof t_stereo_camera_calibration
 */
void
t_stereo_camera_calibration_destroy(struct t_stereo_camera_calibration *c);

/*!
 * Update the reference counts on a stereo calibration data(s).
 *
 * @param     dst Pointer to a object reference, if the object reference is
 *                non-null will decrement it's counter. The reference that
 *                @p dst points to will be set to @p src.
 * @param[in] src Object to be have it's refcount increased @p dst is set to
 *                this.
 *
 * @relates t_stereo_camera_calibration
 */
static inline void
t_stereo_camera_calibration_reference(struct t_stereo_camera_calibration **dst, struct t_stereo_camera_calibration *src)
{
	struct t_stereo_camera_calibration *old_dst = *dst;

	if (old_dst == src) {
		return;
	}

	if (src) {
		xrt_reference_inc(&src->reference);
	}

	*dst = src;

	if (old_dst) {
		if (xrt_reference_dec(&old_dst->reference)) {
			t_stereo_camera_calibration_destroy(old_dst);
		}
	}
}

/*!
 * Load stereo calibration data from a given file.
 */
bool
t_stereo_camera_calibration_load_v1(FILE *calib_file, struct t_stereo_camera_calibration **out_data);

/*!
 * Save the given stereo calibration data to the given file.
 */
bool
t_stereo_camera_calibration_save_v1(FILE *calib_file, struct t_stereo_camera_calibration *data);


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
t_convert_in_place_y8u8v8_to_r8g8b8(uint32_t width, uint32_t height, size_t stride, void *data_ptr);

void
t_convert_in_place_y8u8v8_to_h8s8v8(uint32_t width, uint32_t height, size_t stride, void *data_ptr);

void
t_convert_in_place_h8s8v8_to_r8g8b8(uint32_t width, uint32_t height, size_t stride, void *data_ptr);


/*
 *
 * Filter functions.
 *
 */

#define T_HSV_SIZE 32
#define T_HSV_STEP (256 / T_HSV_SIZE)

#define T_HSV_DEFAULT_PARAMS()                                                                                         \
	{                                                                                                              \
		{                                                                                                      \
		    {165, 30, 160, 100},                                                                               \
		    {135, 30, 160, 100},                                                                               \
		    {95, 30, 160, 100},                                                                                \
		},                                                                                                     \
		    {128, 80},                                                                                         \
	}

struct t_hsv_filter_color
{
	uint8_t hue_min;
	uint8_t hue_range;

	uint8_t s_min;

	uint8_t v_min;
};

/*!
 * Parameters for constructing an HSV filter.
 * @relates t_hsv_filter
 */
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
t_hsv_build_convert_table(struct t_hsv_filter_params *params, struct t_convert_table *t);

void
t_hsv_build_large_table(struct t_hsv_filter_params *params, struct t_hsv_filter_large_table *t);

void
t_hsv_build_optimized_table(struct t_hsv_filter_params *params, struct t_hsv_filter_optimized_table *t);

static inline uint8_t
t_hsv_filter_sample(struct t_hsv_filter_optimized_table *t, uint32_t y, uint32_t u, uint32_t v)
{
	return t->v[y / T_HSV_STEP][u / T_HSV_STEP][v / T_HSV_STEP];
}

/*!
 * Construct an HSV filter sink.
 * @public @memberof t_hsv_filter
 *
 * @relates xrt_frame_context
 */
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

/*!
 * @public @memberof xrt_tracked_psmv
 */
int
t_psmv_start(struct xrt_tracked_psmv *xtmv);

/*!
 * @public @memberof xrt_tracked_psmv
 */
int
t_psmv_create(struct xrt_frame_context *xfctx,
              struct xrt_colour_rgb_f32 *rgb,
              struct t_stereo_camera_calibration *data,
              struct xrt_tracked_psmv **out_xtmv,
              struct xrt_frame_sink **out_sink);

/*!
 * @public @memberof xrt_tracked_psvr
 */
int
t_psvr_start(struct xrt_tracked_psvr *xtvr);

/*!
 * @public @memberof xrt_tracked_psvr
 */
int
t_psvr_create(struct xrt_frame_context *xfctx,
              struct t_stereo_camera_calibration *data,
              struct xrt_tracked_psvr **out_xtvr,
              struct xrt_frame_sink **out_sink);

/*!
 * @public @memberof xrt_tracked_hand
 */
int
t_hand_create(struct xrt_frame_context *xfctx,
              struct t_stereo_camera_calibration *data,
              struct xrt_tracked_hand **out_xth,
              struct xrt_frame_sink **out_sink);

/*!
 * @public @memberof xrt_tracked_hand
 */
int
t_hand_start(struct xrt_tracked_hand *xth);

/*
 *
 * Camera calibration
 *
 */

/*!
 * Board pattern type.
 */
enum t_board_pattern
{
	T_BOARD_CHECKERS,
	T_BOARD_CIRCLES,
	T_BOARD_ASYMMETRIC_CIRCLES,
};

struct t_calibration_status
{
	//! Is calibration finished?
	bool finished;
	//! Was the target found this frame?
	bool found;
	//! Number of frames collected
	int num_collected;
	//! Number of moving frames before another capture
	int cooldown;
	//! Number of non-moving frames before capture.
	int waits_remaining;
	//! Stereo calibration data that was produced.
	struct t_stereo_camera_calibration *stereo_data;
};

struct t_calibration_params
{
	//! Should we use fisheye version of the calibration functions.
	bool use_fisheye;
	//! Is the camera a stereo sbs camera, mostly for image loading.
	bool stereo_sbs;
	//! What type of pattern are we using for calibration.
	enum t_board_pattern pattern;

	struct
	{
		int cols;
		int rows;
		float size_meters;

		bool subpixel_enable;
		int subpixel_size;
	} checkers;

	struct
	{
		int cols;
		int rows;
		float distance_meters;
	} circles;

	struct
	{
		int cols;
		int rows;
		float diagonal_distance_meters;
	} asymmetric_circles;

	struct
	{
		bool enabled;
		int num_images;
	} load;

	int num_cooldown_frames;
	int num_wait_for;
	int num_collect_total;
	int num_collect_restart;

	/*!
	 * Should we mirror the RGB image?
	 *
	 * Before text is written out, has no effect on actual image capture.
	 */
	bool mirror_rgb_image;

	bool save_images;
};

/*!
 * Sets the calibration parameters to the their default values.
 * @public @memberof t_calibration_params
 */
static inline void
t_calibration_params_default(struct t_calibration_params *p)
{
	// Camera config.
	p->use_fisheye = false;
	p->stereo_sbs = true;

	// Which board should we calibrate against.
	p->pattern = T_BOARD_CHECKERS;

	// Checker board.
	p->checkers.cols = 9;
	p->checkers.rows = 7;
	p->checkers.size_meters = 0.025f;
	p->checkers.subpixel_enable = true;
	p->checkers.subpixel_size = 5;

	// Symmetrical circles.
	p->circles.cols = 9;
	p->circles.rows = 7;
	p->circles.distance_meters = 0.025f;

	// Asymmetrical circles.
	p->asymmetric_circles.cols = 5;
	p->asymmetric_circles.rows = 17;
	p->asymmetric_circles.diagonal_distance_meters = 0.02f;

	// Loading of images.
	p->load.enabled = false;
	p->load.num_images = 20;

	// Frame collection info.
	p->num_cooldown_frames = 20;
	p->num_wait_for = 5;
	p->num_collect_total = 20;
	p->num_collect_restart = 1;

	// Misc.
	p->mirror_rgb_image = false;
	p->save_images = true;
}

/*!
 * @brief Create the camera calibration frame sink.
 *
 * @param xfctx Context for frame transport.
 * @param params Parameters to use during calibration. Values copied, pointer
 * not retained.
 * @param status Optional pointer to structure for status information. Pointer
 * retained, and pointed-to struct modified.
 * @param gui Frame sink
 * @param out_sink Output: created frame sink.
 *
 * @relates xrt_frame_context
 */
int
t_calibration_stereo_create(struct xrt_frame_context *xfctx,
                            const struct t_calibration_params *params,
                            struct t_calibration_status *status,
                            struct xrt_frame_sink *gui,
                            struct xrt_frame_sink **out_sink);


/*
 *
 * Sink creation functions.
 *
 */

/*!
 * @relates xrt_frame_context
 */
int
t_convert_yuv_or_yuyv_create(struct xrt_frame_sink *next, struct xrt_frame_sink **out_sink);



/*!
 * @relates xrt_frame_context
 */
int
t_debug_hsv_picker_create(struct xrt_frame_context *xfctx,
                          struct xrt_frame_sink *passthrough,
                          struct xrt_frame_sink **out_sink);

/*!
 * @relates xrt_frame_context
 */
int
t_debug_hsv_viewer_create(struct xrt_frame_context *xfctx,
                          struct xrt_frame_sink *passthrough,
                          struct xrt_frame_sink **out_sink);

/*!
 * @relates xrt_frame_context
 */
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
