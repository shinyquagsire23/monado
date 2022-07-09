// Copyright 2019-2021, Collabora, Ltd.
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

#include "util/u_logging.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"
#include "util/u_misc.h"

#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @addtogroup aux_tracking
 * @{
 */


/*
 *
 * Pre-declare
 *
 */

typedef struct cJSON cJSON;
struct xrt_slam_sinks;
struct xrt_tracked_psmv;
struct xrt_tracked_psvr;
struct xrt_tracked_slam;


/*
 *
 * Calibration data.
 *
 */

//! Maximum size of rectilinear distortion coefficient array
#define XRT_DISTORTION_MAX_DIM (14)

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

	//! Number of distortion parameters (non-fisheye).
	size_t distortion_num;

	//! Rectilinear distortion coefficients: k1, k2, p1, p2[, k3[, k4, k5, k6[, s1, s2, s3, s4[, Tx, Ty]]]]
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
 * Allocates a new stereo calibration data, unreferences the old data pointed to by @p out_c.
 *
 * Also initializes t_camera_calibration::distortion_num in t_stereo_camera_calibration::view, only 5 and 14 is
 * accepted.
 *
 * @public @memberof t_stereo_camera_calibration
 */
void
t_stereo_camera_calibration_alloc(struct t_stereo_camera_calibration **out_c, uint32_t distortion_num);

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
 * @param[in,out] dst Pointer to a object reference: if the object reference is
 *                non-null will decrement its counter. The reference that
 *                @p dst points to will be set to @p src.
 * @param[in] src New object for @p dst to refer to (may be null).
 *                If non-null, will have its refcount increased.
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
 * Small helper function that dumps one camera calibration data to logging.
 *
 * @relates t_camera_calibration
 */
void
t_camera_calibration_dump(struct t_camera_calibration *c);

/*!
 * Small helper function that dumps the stereo calibration data to logging.
 *
 * @relates t_stereo_camera_calibration
 */
void
t_stereo_camera_calibration_dump(struct t_stereo_camera_calibration *c);

/*!
 * Load stereo calibration data from a given file in v1 format (binary).
 *
 * @relates t_stereo_camera_calibration
 */
bool
t_stereo_camera_calibration_load_v1(FILE *calib_file, struct t_stereo_camera_calibration **out_data);

/*!
 * Save the given stereo calibration data to the given file in v1 format (binary).
 *
 * @relates t_stereo_camera_calibration
 */
bool
t_stereo_camera_calibration_save_v1(FILE *calib_file, struct t_stereo_camera_calibration *data);

/*!
 * Parse the json object in v2 format into stereo calibration data.
 *
 * @relates t_stereo_camera_calibration
 */
bool
t_stereo_camera_calibration_from_json_v2(cJSON *json, struct t_stereo_camera_calibration **out_stereo);

/*!
 * Convert the given stereo calibration data into a json object in v2 format.
 *
 * @relates t_stereo_camera_calibration
 */
bool
t_stereo_camera_calibration_to_json_v2(cJSON **out_cjson, struct t_stereo_camera_calibration *data);


/*!
 * Load stereo calibration data from a given file path.
 *
 * @relates t_stereo_camera_calibration
 */
bool
t_stereo_camera_calibration_load(const char *calib_path, struct t_stereo_camera_calibration **out_data);

/*!
 * Save the given stereo calibration data to the given file path.
 *
 * @relates t_stereo_camera_calibration
 */
bool
t_stereo_camera_calibration_save(const char *calib_path, struct t_stereo_camera_calibration *data);


/*
 *
 * IMU calibration data.
 *
 */

/*!
 * @brief Parameters for accelerometer and gyroscope calibration.
 * @see slam_tracker::imu_calibration for a more detailed description and references.
 */
struct t_inertial_calibration
{
	//! Linear transformation for raw measurements alignment and scaling.
	double transform[3][3];

	//! Offset to apply to raw measurements.
	double offset[3];

	//! Modeled sensor bias. @see slam_tracker::imu_calibration.
	double bias_std[3];

	//! Modeled measurement noise. @see slam_tracker::imu_calibration.
	double noise_std[3];
};

/*!
 * @brief Combined IMU calibration data.
 */
struct t_imu_calibration
{
	//! Accelerometer calibration data.
	struct t_inertial_calibration accel;

	//! Gyroscope calibration data.
	struct t_inertial_calibration gyro;
};
/*!
 * Prints a @ref t_inertial_calibration struct
 *
 * @relates t_camera_calibration
 */
void
t_inertial_calibration_dump(struct t_inertial_calibration *c);

/*!
 * Small helper function that dumps the imu calibration data to logging.
 *
 * @relates t_camera_calibration
 */
void
t_imu_calibration_dump(struct t_imu_calibration *c);


/*
 *
 * Conversion functions.
 *
 */

struct t_convert_table
{
	uint8_t v[256][256][256][3]; // nolint(readability-magic-numbers)
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
 * @see xrt_frame_context
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
 * SLAM prediction type. Naming scheme as follows:
 * P: position, O: orientation, A: angular velocity, L: linear velocity
 * S: From SLAM poses (slow, precise), I: From IMU data (fast, noisy)
 *
 * @see xrt_tracked_slam
 */
enum t_slam_prediction_type
{
	SLAM_PRED_NONE = 0,    //!< No prediction, always return the last SLAM tracked pose
	SLAM_PRED_SP_SO_SA_SL, //!< Predicts from last two SLAM poses only
	SLAM_PRED_SP_SO_IA_SL, //!< Predicts from last SLAM pose with angular velocity computed from IMU
	SLAM_PRED_SP_SO_IA_IL, //!< Predicts from last SLAM pose with angular and linear velocity computed from IMU
	SLAM_PRED_COUNT,
};

/*!
 * This struct complements calibration data from @ref
 * t_stereo_camera_calibration and @ref t_imu_calibration
 *
 * @see xrt_tracked_slam
 */
struct t_slam_calib_extras
{
	double imu_frequency; //! IMU samples per second
	struct
	{
		double frequency;                //!< Camera FPS
		struct xrt_matrix_4x4 T_imu_cam; //!< Transform IMU to camera. Column major.
		float rpmax;                     //!< Used for rt8 calibrations. Rpmax or "metric_radius" property.
	} cams[2];
};

/*!
 * SLAM tracker configuration.
 *
 * @see xrt_tracked_slam
 */
struct t_slam_tracker_config
{
	enum u_logging_level log_level; //!< SLAM tracking logging level
	const char *slam_config;        //!< Config file path, format is specific to the SLAM implementation in use
	bool submit_from_start;         //!< Whether to submit data to the SLAM tracker without user action
	enum t_slam_prediction_type prediction; //!< Which level of prediction to use
	bool write_csvs;                        //!< Whether to enable CSV writers from the start for later analysis
	const char *csv_path;                   //!< Path to write CSVs to
	bool timing_stat;                       //!< Enable timing metric in external system
	bool features_stat;                     //!< Enable feature metric in external system

	// Instead of a slam_config file you can set custom calibration data
	const struct t_stereo_camera_calibration *stereo_calib; //!< Camera calibration data
	const struct t_imu_calibration *imu_calib;              //!< IMU calibration data
	const struct t_slam_calib_extras *extra_calib;          //!< Extra calibration data
};

/*!
 * Fills in a @ref t_slam_tracker_config with default values.
 *
 * @see xrt_tracked_Slam
 */
void
t_slam_fill_default_config(struct t_slam_tracker_config *config);

/*!
 * @public @memberof xrt_tracked_slam
 */
int
t_slam_create(struct xrt_frame_context *xfctx,
              struct t_slam_tracker_config *config,
              struct xrt_tracked_slam **out_xts,
              struct xrt_slam_sinks **out_sink);

/*!
 * @public @memberof xrt_tracked_slam
 */
int
t_slam_start(struct xrt_tracked_slam *xts);

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
	//! Sector based checker board, using `cv::findChessboardCornersSB`.
	T_BOARD_SB_CHECKERS,
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
		float size_meters;

		bool marker;
		bool normalize_image;
	} sb_checkers;

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
void
t_calibration_gui_params_default(struct t_calibration_params *p);

void
t_calibration_gui_params_load_or_default(struct t_calibration_params *p);

void
t_calibration_gui_params_to_json(cJSON **out_json, struct t_calibration_params *p);

void
t_calibration_gui_params_parse_from_json(const cJSON *params, struct t_calibration_params *p);

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
 * @see xrt_frame_context
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
 * @see xrt_frame_context
 */
int
t_convert_yuv_or_yuyv_create(struct xrt_frame_sink *next, struct xrt_frame_sink **out_sink);



/*!
 * @see xrt_frame_context
 */
int
t_debug_hsv_picker_create(struct xrt_frame_context *xfctx,
                          struct xrt_frame_sink *passthrough,
                          struct xrt_frame_sink **out_sink);

/*!
 * @see xrt_frame_context
 */
int
t_debug_hsv_viewer_create(struct xrt_frame_context *xfctx,
                          struct xrt_frame_sink *passthrough,
                          struct xrt_frame_sink **out_sink);

/*!
 * @see xrt_frame_context
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
