// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Mercury main header!
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */

#pragma once

#include "hg_interface.h"

#include "tracking/t_calibration_opencv.hpp"

#include "tracking/t_hand_tracking.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"

#include "math/m_api.h"
#include "math/m_vec2.h"
#include "math/m_vec3.h"
#include "math/m_mathinclude.h"

#include "util/u_frame_times_widget.h"
#include "util/u_logging.h"
#include "util/u_sink.h"
#include "util/u_template_historybuf.hpp"
#include "util/u_worker.h"
#include "util/u_trace_marker.h"
#include "util/u_debug.h"
#include "util/u_frame.h"
#include "util/u_var.h"

#include "util/u_template_historybuf.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <opencv2/opencv.hpp>
#include <onnxruntime_c_api.h>

#include "kine_common.hpp"
#include "kine_lm/lm_interface.hpp"
#include "kine_ccdik/ccdik_interface.hpp"



namespace xrt::tracking::hand::mercury {

using namespace xrt::auxiliary::util;

#define HG_TRACE(hgt, ...) U_LOG_IFL_T(hgt->log_level, __VA_ARGS__)
#define HG_DEBUG(hgt, ...) U_LOG_IFL_D(hgt->log_level, __VA_ARGS__)
#define HG_INFO(hgt, ...) U_LOG_IFL_I(hgt->log_level, __VA_ARGS__)
#define HG_WARN(hgt, ...) U_LOG_IFL_W(hgt->log_level, __VA_ARGS__)
#define HG_ERROR(hgt, ...) U_LOG_IFL_E(hgt->log_level, __VA_ARGS__)


static const cv::Scalar RED(255, 30, 30);
static const cv::Scalar YELLOW(255, 255, 0);
static const cv::Scalar PINK(255, 0, 255);

static const cv::Scalar colors[2] = {YELLOW, RED};

#undef USE_NCNN

// Forward declaration for ht_view
struct HandTracking;
struct ht_view;

// Using the compiler to stop me from getting 2D space mixed up with 3D space.
struct Hand2D
{
	struct xrt_vec2 kps[21];
	float confidences[21];
};

struct Hand3D
{
	struct xrt_vec3 kps[21];
};

struct onnx_wrap
{
	const OrtApi *api = nullptr;
	OrtEnv *env = nullptr;

	OrtMemoryInfo *meminfo = nullptr;
	OrtSession *session = nullptr;
	OrtValue *tensor = nullptr;
	float *data;
	int64_t input_shape[4];
	const char *input_name;
};

struct hand_bounding_box
{
	xrt_vec2 center;
	float size_px;
	bool found;
	bool confidence;
};

struct hand_detection_run_info
{
	ht_view *view;
	hand_bounding_box *outputs[2];
};


struct keypoint_output
{
	Hand2D hand_px_coord;
	Hand2D hand_tan_space;
	float confidences[21];
};

struct keypoint_estimation_run_info
{
	ht_view *view;
	bool hand_idx;
};

struct ht_view
{
	HandTracking *hgt;
	onnx_wrap detection;
	onnx_wrap keypoint[2];
	int view;

	struct t_camera_extra_info_one_view camera_info;

	cv::Mat distortion;
	cv::Matx<double, 3, 3> cameraMatrix;
	cv::Matx33d rotate_camera_to_stereo_camera; // R1 or R2

	cv::Mat run_model_on_this;
	cv::Mat debug_out_to_this;

	struct hand_bounding_box bboxes_this_frame[2]; // left, right

	struct keypoint_estimation_run_info run_info[2];

	struct keypoint_output keypoint_outputs[2];
};

struct hand_history
{
	HistoryBuffer<xrt_hand_joint_set, 5> hands;
	HistoryBuffer<uint64_t, 5> timestamps;
};

struct output_struct_one_frame
{
	xrt_vec2 left[21];
	float confidences_left[21];
	xrt_vec2 right[21];
	float confidences_right[21];
};

struct hand_size_refinement
{
	int num_hands;
	float out_hand_size;
	float out_hand_confidence;
	float hand_size_refinement_schedule_x = 0;
	float hand_size_refinement_schedule_y = 0;
};

struct model_output_visualizers
{
	// After setup, these reference the same piece of memory.
	cv::Mat mat;
	xrt_frame *xrtframe = NULL;

	// After pushing to the debug UI, we reference the frame here so that we can copy memory out of it for next
	// frame.
	xrt_frame *old_frame = NULL;
};

/*!
 * Main class of Mercury hand tracking.
 *
 * @ingroup aux_tracking
 */
struct HandTracking
{
public:
	// Base thing, has to be first.
	t_hand_tracking_sync base = {};

	struct u_sink_debug debug_sink_ann = {};
	struct u_sink_debug debug_sink_model = {};

	float multiply_px_coord_for_undistort;

	bool use_fisheye;

	struct t_stereo_camera_calibration *calib;

	struct xrt_size calibration_one_view_size_px = {};

	// So that we can calibrate cameras at 1280x800 but ship images over USB at 640x400
	struct xrt_size last_frame_one_view_size_px = {};

#ifdef USE_NCNN
	ncnn_net_t net;
	ncnn_net_t net_keypoint;
#endif

	struct ht_view views[2] = {};

	struct model_output_visualizers visualizers;

	u_worker_thread_pool *pool;

	u_worker_group *group;


	float baseline = {};
	xrt_pose hand_pose_camera_offset = {};

	uint64_t current_frame_timestamp = {};

	bool debug_scribble = false;

	char models_folder[1024];

	enum u_logging_level log_level = U_LOGGING_INFO;

	lm::KinematicHandLM *kinematic_hands[2];
	ccdik::KinematicHandCCDIK *kinematic_hands_ccdik[2];

	// struct hand_detection_state_tracker st_det[2] = {};
	bool hand_seen_before[2] = {false, false};
	bool last_frame_hand_detected[2] = {false, false};
	bool this_frame_hand_detected[2] = {false, false};

	struct hand_history histories[2];

	int detection_counter = 0;

	struct hand_size_refinement refinement = {};
	// Moses hand size is ~0.095; they has big-ish hands so let's do 0.09
	float target_hand_size = 0.09;


	xrt_frame *debug_frame;

	void (*keypoint_estimation_run_func)(void *);



	struct xrt_pose left_in_right = {};

	u_frame_times_widget ft_widget = {};

	struct
	{
		bool new_user_event = false;
		struct u_var_draggable_f32 dyn_radii_fac;
		struct u_var_draggable_f32 dyn_joint_y_angle_error;
		struct u_var_draggable_f32 amount_to_lerp_prediction;
		bool scribble_predictions_into_this_frame = false;
		bool scribble_keypoint_model_outputs = false;
		bool scribble_optimizer_outputs = true;
		bool always_run_detection_model = false;
		bool use_ccdik = false;
		int max_num_outside_view = 3;
	} tuneable_values;


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


void
init_hand_detection(HandTracking *hgt, onnx_wrap *wrap);

void
run_hand_detection(void *ptr);

void
init_keypoint_estimation(HandTracking *hgt, onnx_wrap *wrap);


void
run_keypoint_estimation(void *ptr);


void
init_keypoint_estimation_new(HandTracking *hgt, onnx_wrap *wrap);

void
run_keypoint_estimation_new(void *ptr);

void
release_onnx_wrap(onnx_wrap *wrap);

} // namespace xrt::tracking::hand::mercury
