// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Defines and common includes for camera-based hand tracker
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

#include "ht_interface.h"
#include "os/os_threading.h"

#include "xrt/xrt_device.h"
#include "xrt/xrt_prober.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_frameserver.h"

#include "math/m_api.h"
#include "math/m_vec3.h"
#include "math/m_filter_one_euro.h"

#include "util/u_var.h"
#include "util/u_json.h"
#include "util/u_sink.h"
#include "util/u_debug.h"
#include "util/u_device.h"

#include "util/u_template_historybuf.hpp"

#ifdef XRT_HAVE_GST
#include "gstreamer/gst_pipeline.h"
#include "gstreamer/gst_sink.h"
#endif

#include <opencv2/opencv.hpp>

#include "core/session/onnxruntime_c_api.h"

#include <future>
#include <vector>

using namespace xrt::auxiliary::util;

#define HT_TRACE(htd, ...) U_LOG_XDEV_IFL_T(&htd->base, htd->log_level, __VA_ARGS__)
#define HT_DEBUG(htd, ...) U_LOG_XDEV_IFL_D(&htd->base, htd->log_level, __VA_ARGS__)
#define HT_INFO(htd, ...) U_LOG_XDEV_IFL_I(&htd->base, htd->log_level, __VA_ARGS__)
#define HT_WARN(htd, ...) U_LOG_XDEV_IFL_W(&htd->base, htd->log_level, __VA_ARGS__)
#define HT_ERROR(htd, ...) U_LOG_XDEV_IFL_E(&htd->base, htd->log_level, __VA_ARGS__)

// #define ht_


// To make clang-tidy happy
#define opencv_distortion_param_num 4

/*
 *
 * Compile-time defines to choose where to get camera frames from and what kind of output to give out
 *
 */
#undef EXPERIMENTAL_DATASET_RECORDING

#define FCMIN_BBOX_ORIENTATION 3.0f
#define FCMIN_D_BB0X_ORIENTATION 10.0f
#define BETA_BB0X_ORIENTATION 0.0f

// #define FCMIN_BBOX_POSITION 15.0f
// #define FCMIN_D_BB0X_POSITION 12.0f
// #define BETA_BB0X_POSITION 0.3f

#define FCMIN_BBOX_POSITION 30.0f
#define FCMIN_D_BB0X_POSITION 25.0f
#define BETA_BB0X_POSITION 0.6f



#define FCMIN_HAND 4.0f
#define FCMIN_D_HAND 12.0f
#define BETA_HAND 0.05f

#ifdef __cplusplus
extern "C" {
#endif

enum HandJoint7Keypoint
{
	WRIST_7KP = 0,
	INDEX_7KP = 1,
	MIDDLE_7KP = 2,
	RING_7KP = 3,
	LITTLE_7KP = 4,
	THUMB_METACARPAL_7KP = 5,
	THMB_PROXIMAL_7KP = 6
};

enum HandJoint21Keypoint
{
	WRIST = 0,

	THMB_MCP = 1,
	THMB_PXM = 2,
	THMB_DST = 3,
	THMB_TIP = 4,

	INDX_PXM = 5,
	INDX_INT = 6,
	INDX_DST = 7,
	INDX_TIP = 8,

	MIDL_PXM = 9,
	MIDL_INT = 10,
	MIDL_DST = 11,
	MIDL_TIP = 12,

	RING_PXM = 13,
	RING_INT = 14,
	RING_DST = 15,
	RING_TIP = 16,

	LITL_PXM = 17,
	LITL_INT = 18,
	LITL_DST = 19,
	LITL_TIP = 20
};

struct Palm7KP
{
	struct xrt_vec2 kps[7];
	float confidence; // BETWEEN 0 and 1. okay???? okay????!???
};

struct DetectionModelOutput
{
	float rotation;
	float size;
	xrt_vec2 center;
	Palm7KP palm;

	cv::Matx23f warp_there;
	cv::Matx23f warp_back;
};

// To keep you on your toes. *Don't* think the 2D hand is the same as the 3D!
struct Hand2D
{
	struct xrt_vec3 kps[21];
	// Third value is depth from ML model. Do not believe the depth.
};

struct Hand3D
{
	struct xrt_vec3 kps[21];
	float y_disparity_error;
	float flow_error;
	int idx_l;
	int idx_r;
	bool rejected_by_smush; // init to false.

	float handedness;
	uint64_t timestamp;
};


struct HandHistory3D
{
	// Index 0 is current frame, index 1 is last frame, index 2 is second to last frame.
	// No particular reason to keep the last 5 frames. we only really only use the current and last one.
	float handedness;
	bool have_prev_hand = false;
	double prev_dy;
	uint64_t prev_ts_for_alpha; // also in last_hands_unfiltered[0] but go away.

	uint64_t first_ts;
	uint64_t prev_filtered_ts;

	HistoryBuffer<Hand3D, 10> last_hands_unfiltered;
	HistoryBuffer<Hand3D, 10> last_hands_filtered;

	// Euro filter for 21kps.
	m_filter_euro_vec3 filters[21];
	int uuid;
};

struct HandHistory2DBBox
{
	// Ugh, I should definitely iterate these somehow...
	// m_filter_euro_vec2 m_filter_wrist;
	// m_filter_euro_vec2 m_filter_index;
	// m_filter_euro_vec2 m_filter_middle;
	// m_filter_euro_vec2 m_filter_pinky;

	m_filter_euro_vec2 m_filter_center;
	m_filter_euro_vec2 m_filter_direction;

	HistoryBuffer<xrt_vec2, 50> wrist_unfiltered;
	HistoryBuffer<xrt_vec2, 50> index_unfiltered;
	HistoryBuffer<xrt_vec2, 50> middle_unfiltered;
	HistoryBuffer<xrt_vec2, 50> pinky_unfiltered;
	bool htAlgorithm_approves = false;
};


struct ModelInfo
{
	OrtSession *session = nullptr;
	OrtMemoryInfo *memoryInfo = nullptr;
	// std::vector's don't make too much sense here, but they're oh so easy
	std::vector<int64_t> input_shape;
	size_t input_size_bytes;
	std::vector<const char *> output_names;
	std::vector<const char *> input_names;
};


// Forward declaration for ht_view
struct ht_device;

struct ht_view
{
	ht_device *htd;
	int view; // :)))

	// Loaded from config file
	cv::Matx<double, opencv_distortion_param_num, 1> distortion;
	cv::Matx<double, 3, 3> cameraMatrix;
	cv::Matx33d rotate_camera_to_stereo_camera; // R1 or R2

	cv::Mat run_model_on_this;
	cv::Mat debug_out_to_this;

	std::vector<HandHistory2DBBox> bbox_histories;

	struct ModelInfo detection_model;
	std::vector<Palm7KP> (*run_detection_model)(struct ht_view *htv, cv::Mat &img);

	struct ModelInfo keypoint_model;
	// The cv::mat is passed by value, *not* passed by reference or by pointer;
	// in the tight loop that sets these off we reuse that cv::Mat; changing the data pointer as all the models are
	// running is... going to wreak havoc let's say that.
	Hand2D (*run_keypoint_model)(struct ht_view *htv, cv::Mat img);
};

enum ht_detection_scribble
{
	HT_DETECTION_SCRIBBLE_ALL,
	HT_DETECTION_SCRIBBLE_SOME,
	HT_DETECTION_SCRIBBLE_NONE
};

struct ht_dynamic_config
{
	char name[64];
	struct u_var_draggable_f32 hand_fc_min;
	struct u_var_draggable_f32 hand_fc_min_d;
	struct u_var_draggable_f32 hand_beta;
	struct u_var_draggable_f32 max_vel;
	struct u_var_draggable_f32 max_acc;
	struct u_var_draggable_f32 nms_iou;
	struct u_var_draggable_f32 nms_threshold;
	struct u_var_draggable_f32 new_detection_threshold;
	bool scribble_raw_detections;
	bool scribble_nms_detections;
	bool scribble_2d_keypoints;
	bool scribble_bounding_box;
};

struct ht_startup_config
{
	bool palm_detection_use_mediapipe = false;
	bool keypoint_estimation_use_mediapipe = false;
	enum xrt_format desired_format;
	char model_slug[1024];
};

// This is all ad-hoc! Review very welcome!
struct ht_device
{
	struct xrt_device base;

	struct xrt_tracking_origin tracking_origin; // probably cargo-culted

	struct xrt_frame_sink sink;
	struct xrt_frame_node node;

	struct u_sink_debug debug_sink; // this must be bad.


	struct
	{
		struct xrt_frame_context xfctx;

		struct xrt_fs *xfs;

		struct xrt_fs_mode mode;

		struct xrt_prober *prober;

		struct xrt_size one_view_size_px;
	} camera;



#if defined(EXPERIMENTAL_DATASET_RECORDING)
	struct
	{
		struct u_var_button start_json_record;
	} gui;
	struct
	{
		struct gstreamer_pipeline *gp;
		struct gstreamer_sink *gs;
		struct xrt_frame_sink *sink;
		struct xrt_frame_context xfctx;
		uint64_t offset_ns;
		uint64_t last_frame_ns;
		uint64_t current_index;

		cJSON *output_root;
		cJSON *output_array;
	} gst;
#endif



	const OrtApi *ort_api;
	OrtEnv *ort_env;

	struct xrt_frame *frame_for_process;
	cv::Mat *mat_for_process;

	struct ht_view views[2];

	float baseline;
	struct xrt_quat stereo_camera_to_left_camera;

	uint64_t current_frame_timestamp; // SUPER dumb.

	std::vector<HandHistory3D> histories_3d;

	struct os_mutex openxr_hand_data_mediator;
	struct xrt_hand_joint_set hands_for_openxr[2];
	uint64_t hands_for_openxr_timestamp;

	// Only change these when you have unlocked_between_frames, ie. when the hand tracker is between frames.
	bool tracking_should_die;
	bool tracking_should_record_dataset;
	struct os_mutex unlocked_between_frames;

	// Change this whenever you want
	bool debug_scribble = true;

	ht_run_type run_type;



	struct ht_startup_config startup_config;
	struct ht_dynamic_config dynamic_config;


	int dynamic_config_to_use;



	enum u_logging_level log_level;
};

static inline struct ht_device *
ht_device(struct xrt_device *xdev)
{
	return (struct ht_device *)xdev;
}


#ifdef __cplusplus
}
#endif
