// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Old RGB hand tracking header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */

#pragma once

#include "tracking/t_hand_tracking.h"

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

#include <opencv2/opencv.hpp>

#include <vector>
namespace xrt::tracking::hand::old_rgb {

using namespace xrt::auxiliary::util;

#define HT_TRACE(htd, ...) U_LOG_IFL_T(htd->log_level, __VA_ARGS__)
#define HT_DEBUG(htd, ...) U_LOG_IFL_D(htd->log_level, __VA_ARGS__)
#define HT_INFO(htd, ...) U_LOG_IFL_I(htd->log_level, __VA_ARGS__)
#define HT_WARN(htd, ...) U_LOG_IFL_W(htd->log_level, __VA_ARGS__)
#define HT_ERROR(htd, ...) U_LOG_IFL_E(htd->log_level, __VA_ARGS__)

#undef EXPERIMENTAL_DATASET_RECORDING

#define FCMIN_BBOX_ORIENTATION 3.0f
#define FCMIN_D_BB0X_ORIENTATION 10.0f
#define BETA_BB0X_ORIENTATION 0.0f

#define FCMIN_BBOX_POSITION 30.0f
#define FCMIN_D_BB0X_POSITION 25.0f
#define BETA_BB0X_POSITION 0.01f



#define FCMIN_HAND 4.0f
#define FCMIN_D_HAND 12.0f
#define BETA_HAND 0.0083f

class ht_model;

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
	float confidence; // between 0 and 1
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
	uint64_t prev_ts_for_alpha; // also in last_hands_unfiltered.back() but go away.

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
	m_filter_euro_vec2 m_filter_center;
	m_filter_euro_vec2 m_filter_direction;

	HistoryBuffer<xrt_vec2, 50> wrist_unfiltered;
	HistoryBuffer<xrt_vec2, 50> index_unfiltered;
	HistoryBuffer<xrt_vec2, 50> middle_unfiltered;
	HistoryBuffer<xrt_vec2, 50> pinky_unfiltered;
	bool htAlgorithm_approves = false;
};

// Forward declaration for ht_view
struct HandTracking;

struct ht_view
{
	HandTracking *htd;
	ht_model *htm;
	int view;

	cv::Matx<double, 4, 1> distortion;
	cv::Matx<double, 3, 3> cameraMatrix;
	cv::Matx33d rotate_camera_to_stereo_camera; // R1 or R2

	cv::Mat run_model_on_this;
	cv::Mat debug_out_to_this;

	std::vector<HandHistory2DBBox> bbox_histories;
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

/*!
 * Main class of old style RGB hand tracking.
 *
 * @ingroup aux_tracking
 */
struct HandTracking
{
public:
	// Base thing, has to be first.
	t_hand_tracking_sync base = {};

	struct u_sink_debug debug_sink = {};

	struct xrt_size one_view_size_px = {};

#if defined(EXPERIMENTAL_DATASET_RECORDING)
	struct
	{
		struct u_var_button start_json_record = {};
	} gui = {};

	struct
	{
		struct gstreamer_pipeline *gp = nullptr;
		struct gstreamer_sink *gs = nullptr;
		struct xrt_frame_sink *sink = nullptr;
		struct xrt_frame_context xfctx = {};
		uint64_t offset_ns = {};
		uint64_t last_frame_ns = {};
		uint64_t current_index = {};

		cJSON *output_root = nullptr;
		cJSON *output_array = nullptr;
	} gst = {};
#endif

	struct ht_view views[2] = {};

	float baseline = {};
	struct xrt_quat stereo_camera_to_left_camera = {};

	uint64_t current_frame_timestamp = {};

	std::vector<HandHistory3D> histories_3d = {};

	struct os_mutex openxr_hand_data_mediator = {};
	struct xrt_hand_joint_set hands_for_openxr[2] = {};
	uint64_t hands_for_openxr_timestamp = {};

	// Only change these when you have unlocked_between_frames, ie. when the hand tracker is between frames.
	bool tracking_should_die = {};
	bool tracking_should_record_dataset = {};
	struct os_mutex unlocked_between_frames = {};

	// Change this whenever you want
	volatile bool debug_scribble = true;

	struct ht_startup_config startup_config = {};
	struct ht_dynamic_config dynamic_config = {};

	enum u_logging_level log_level = U_LOGGING_INFO;

public:
	explicit HandTracking();
	~HandTracking();

	static inline HandTracking &
	fromC(t_hand_tracking_sync *ht_sync)
	{
		return *reinterpret_cast<HandTracking *>(ht_sync);
	}

	static void
	cCallbackProcess(struct t_hand_tracking_sync *ht_sync,
	                 struct xrt_frame *left_frame,
	                 struct xrt_frame *right_frame,
	                 struct xrt_hand_joint_set *out_left_hand,
	                 struct xrt_hand_joint_set *out_right_hand,
	                 uint64_t *out_timestamp_ns);

	static void
	cCallbackDestroy(t_hand_tracking_sync *ht_sync);
};


} // namespace xrt::tracking::hand::old_rgb
