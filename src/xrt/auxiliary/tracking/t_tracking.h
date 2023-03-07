// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tracking API interface.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_tracking
 */

#pragma once

#include "util/u_logging.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_tracking.h"
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
 * @brief The distortion model this camera calibration falls under.
 * @todo Add RiftS's Fisheye62 to this enumerator once we have native support for it in our hand tracking and SLAM.
 * @todo Feel free to add support for T_DISTORTION_OPENCV_RADTAN_4 or T_DISTORTION_OPENCV_RADTAN_12 whenever you have a
 * camera that uses those.
 */
enum t_camera_distortion_model
{
	/*!
	 * OpenCV's radial-tangential distortion model. Exactly equivalent to the distortion model from OpenCV's calib3d
	 * module with just the first five parameters. This may be reinterpreted as RT8 with the last three parameters
	 * zeroed out, which is 100% valid and results in exactly equivalent (un)projections.
	 *
	 * Parameters:
	 *
	 * \f[(k_1, k_2, p_1, p_2, k_3)\f]
	 */
	T_DISTORTION_OPENCV_RADTAN_5,

	/*!
	 * OpenCV's radial-tangential distortion model. Exactly equivalent to the distortion model from OpenCV's calib3d
	 * module, with just the first 8 parameters.
	 * Parameters:
	 *
	 * \f[(k_1, k_2, p_1, p_2, k_3, k_4, k_5, k_6)\f]
	 */
	T_DISTORTION_OPENCV_RADTAN_8,

	/*!
	 * OpenCV's radial-tangential distortion model. Exactly equivalent to the distortion model from OpenCV's calib3d
	 * module, with all 14 parameters.
	 *
	 * In practice this is reinterpreted as RT8 because the last 6 parameters are almost always approximately 0.
	 *
	 * @todo Feel free to implement RT14 (un)projection functions if you have a camera that actually has a tilted
	 * sensor.
	 *
	 * Parameters:
	 *
	 * \f[(k_1, k_2, p_1, p_2, k_3, k_4, k_5, k_6, s_1, s_2, s_3, s_4, \tau_x, \tau_y)\f]
	 *
	 * All known factory-calibrated Luxonis cameras use this distortion model, and in all known cases their last 6
	 * parameters are approximately 0.
	 *
	 */
	T_DISTORTION_OPENCV_RADTAN_14,

	/*!
	 * Juho Kannalla and Sami Sebastian Brandt's fisheye distortion model. Exactly equivalent to the distortion
	 * model from OpenCV's calib3d/fisheye module.
	 *
	 * Parameters:
	 *
	 * \f[(k_1, k_2, k_3, k_4)\f]
	 *
	 * Many cameras use this model. Here's a non-exhaustive list of cameras Monado knows about that fall under this
	 * model:
	 * * Intel T265
	 * * Valve Index
	 */
	T_DISTORTION_FISHEYE_KB4,

	/*!
	 * Windows Mixed Reality headsets' camera model.
	 *
	 * The model is listed as CALIBRATION_LensDistortionModelRational6KT in the WMR json files, which seems to be
	 * equivalent to Azure-Kinect-Sensor-SDK's K4A_CALIBRATION_LENS_DISTORTION_MODEL_RATIONAL_6KT.
	 *
	 * The only difference between this model and RT8 are codx, cody, and the way p1 and p2 are interpreted. In
	 * practice we reinterpret this as RT8 because those values are almost always approximately 0 for WMR headsets.
	 *
	 * Parameters:
	 *
	 * \f[(k_1, k_2, p_1, p_2, k_3, k_4, k_5, k_6, cod_x, cod_y, rpmax)\f]
	 */
	T_DISTORTION_WMR,
};


/*!
 * Stringifies a @ref enum t_camera_distortion_model
 * @param model The distortion model to be stringified
 * @return The distortion model as a string
 */
static inline const char *
t_stringify_camera_distortion_model(const enum t_camera_distortion_model model)
{
	switch (model) {
	case T_DISTORTION_OPENCV_RADTAN_5: return "T_DISTORTION_OPENCV_RADTAN_5"; break;
	case T_DISTORTION_OPENCV_RADTAN_8: return "T_DISTORTION_OPENCV_RADTAN_8"; break;
	case T_DISTORTION_OPENCV_RADTAN_14: return "T_DISTORTION_OPENCV_RADTAN_14"; break;
	case T_DISTORTION_WMR: return "T_DISTORTION_WMR"; break;
	case T_DISTORTION_FISHEYE_KB4: return "T_DISTORTION_FISHEYE_KB4"; break;
	default: U_LOG_E("Invalid distortion_model! %d", model); return "INVALID";
	}
}

/*!
 * Returns the number of parameters needed for this @ref enum t_camera_distortion_model to be held by an OpenCV Mat and
 * correctly interpreted by OpenCV's (un)projection functions.
 *
 * @param model The distortion model in question
 * @return The number of distortion coefficients, or 0 if this model cannot be represented inside OpenCV.
 */
static inline size_t
t_num_params_from_distortion_model(const enum t_camera_distortion_model model)
{
	switch (model) {
	case T_DISTORTION_OPENCV_RADTAN_5: return 5; break;
	case T_DISTORTION_OPENCV_RADTAN_8: return 8; break;
	case T_DISTORTION_OPENCV_RADTAN_14: return 14; break;
	case T_DISTORTION_WMR: return 11; break;
	case T_DISTORTION_FISHEYE_KB4: return 4; break;
	default: U_LOG_E("Invalid distortion_model! %d", model); return 0;
	}
}

/*!
 * Parameters for @ref T_DISTORTION_OPENCV_RADTAN_5
 * @ingroup aux_tracking
 */
struct t_camera_calibration_rt5_params
{
	double k1, k2, p1, p2, k3;
};

/*!
 * Parameters for @ref T_DISTORTION_OPENCV_RADTAN_8
 * @ingroup aux_tracking
 */
struct t_camera_calibration_rt8_params
{
	double k1, k2, p1, p2, k3, k4, k5, k6;
};

/*!
 * Parameters for @ref T_DISTORTION_OPENCV_RADTAN_14
 * @ingroup aux_tracking
 */
struct t_camera_calibration_rt14_params
{
	double k1, k2, p1, p2, k3, k4, k5, k6, s1, s2, s3, s4, tx, ty;
};

/*!
 * Parameters for @ref T_DISTORTION_FISHEYE_KB4
 * @ingroup aux_tracking
 */
struct t_camera_calibration_kb4_params
{
	double k1, k2, k3, k4;
};

/*!
 * Parameters for @ref T_DISTORTION_WMR
 * @ingroup aux_tracking
 */
struct t_camera_calibration_wmr_params
{
	double k1, k2, p1, p2, k3, k4, k5, k6, codx, cody, rpmax;
};

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

	union {
		struct t_camera_calibration_rt5_params rt5;
		struct t_camera_calibration_rt8_params rt8;
		struct t_camera_calibration_rt14_params rt14;
		struct t_camera_calibration_kb4_params kb4;
		struct t_camera_calibration_wmr_params wmr;
		double distortion_parameters_as_array[XRT_DISTORTION_MAX_DIM];
	};


	//! Distortion model that this camera uses.
	enum t_camera_distortion_model distortion_model;
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
 * @public @memberof t_stereo_camera_calibration
 */
void
t_stereo_camera_calibration_alloc(struct t_stereo_camera_calibration **out_c,
                                  const enum t_camera_distortion_model distortion_model);

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
 * Extension to camera calibration for SLAM tracking
 *
 * @see xrt_tracked_slam
 */
struct t_slam_camera_calibration
{
	struct t_camera_calibration base;
	struct xrt_matrix_4x4 T_imu_cam; //!< Transform IMU to camera. Column major.
	double frequency;                //!< Camera FPS
};

/*!
 * Extension to IMU calibration for SLAM tracking
 *
 * @see xrt_tracked_slam
 */
struct t_slam_imu_calibration
{
	struct t_imu_calibration base;
	double frequency;
};

/*!
 * Calibration information necessary for SLAM tracking.
 *
 * @see xrt_tracked_slam
 */
struct t_slam_calibration
{
	struct t_slam_imu_calibration imu;                                 //!< IMU calibration data
	struct t_slam_camera_calibration cams[XRT_TRACKING_MAX_SLAM_CAMS]; //!< Calib data of `cam_count` cams
	int cam_count;                                                     //!< Number of cameras
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
	int cam_count;                  //!< Number of cameras in use
	bool slam_ui;                   //!< Whether to open the external UI of the external SLAM system
	bool submit_from_start;         //!< Whether to submit data to the SLAM tracker without user action
	int openvr_groundtruth_device;  //!< If >0, use lighthouse as groundtruth, see @ref enum openvr_device
	enum t_slam_prediction_type prediction; //!< Which level of prediction to use
	bool write_csvs;                        //!< Whether to enable CSV writers from the start for later analysis
	const char *csv_path;                   //!< Path to write CSVs to
	bool timing_stat;                       //!< Enable timing metric in external system
	bool features_stat;                     //!< Enable feature metric in external system

	//!< Instead of a slam_config file you can set custom calibration data
	const struct t_slam_calibration *slam_calib;
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
