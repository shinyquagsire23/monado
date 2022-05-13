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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <opencv2/opencv.hpp>
#include <core/session/onnxruntime_c_api.h>

#include "kine/kinematic_interface.hpp"


namespace xrt::tracking::hand::mercury {

using namespace xrt::auxiliary::util;

DEBUG_GET_ONCE_LOG_OPTION(mercury_log, "MERCURY_LOG", U_LOGGING_WARN)
DEBUG_GET_ONCE_BOOL_OPTION(mercury_use_simdr_keypoint, "MERCURY_USE_SIMDR_KEYPOINT", false)

#define HT_TRACE(htd, ...) U_LOG_IFL_T(htd->log_level, __VA_ARGS__)
#define HT_DEBUG(htd, ...) U_LOG_IFL_D(htd->log_level, __VA_ARGS__)
#define HT_INFO(htd, ...) U_LOG_IFL_I(htd->log_level, __VA_ARGS__)
#define HT_WARN(htd, ...) U_LOG_IFL_W(htd->log_level, __VA_ARGS__)
#define HT_ERROR(htd, ...) U_LOG_IFL_E(htd->log_level, __VA_ARGS__)

#undef USE_NCNN

// Forward declaration for ht_view
struct HandTracking;
struct ht_view;

enum Joint21
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

// Using the compiler to stop me from getting 2D space mixed up with 3D space.
struct Hand2D
{
	struct xrt_vec2 kps[21];
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

struct det_output
{
	xrt_vec2 center;
	float size_px;
	bool found;
};

struct keypoint_output
{
	Hand2D hand_px_coord;
	Hand2D hand_tan_space;
};

struct keypoint_estimation_run_info
{
	ht_view *view;
	bool hand_idx;
};

struct ht_view
{
	HandTracking *htd;
	onnx_wrap detection;
	onnx_wrap keypoint[2];
	int view;

	cv::Mat distortion;
	cv::Matx<double, 3, 3> cameraMatrix;
	cv::Matx33d rotate_camera_to_stereo_camera; // R1 or R2

	cv::Mat run_model_on_this;
	cv::Mat debug_out_to_this;

	struct det_output det_outputs[2];                // left, right
	struct keypoint_estimation_run_info run_info[2]; // Stupid

	struct keypoint_output keypoint_outputs[2];
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

	struct u_sink_debug debug_sink = {};

	float multiply_px_coord_for_undistort;

	bool use_fisheye;

	enum mercury_output_space output_space;

	struct t_stereo_camera_calibration *calib;

	struct xrt_size calibration_one_view_size_px = {};

	// So that we can calibrate cameras at 1280x800 but ship images over USB at 640x400
	struct xrt_size last_frame_one_view_size_px = {};

#ifdef USE_NCNN
	ncnn_net_t net;
	ncnn_net_t net_keypoint;
#endif

	struct ht_view views[2] = {};

	u_worker_thread_pool *pool;

	u_worker_group *group;

	float hand_size;

	float baseline = {};
	struct xrt_quat stereo_camera_to_left_camera = {};

	uint64_t current_frame_timestamp = {}; // SUPER dumb.

	// Change this whenever you want
	volatile bool debug_scribble = true;

	char models_folder[1024];

	enum u_logging_level log_level = U_LOGGING_INFO;

	kine::kinematic_hand_4f *kinematic_hands[2];
	bool last_frame_hand_detected[2] = {false, false};

	xrt_frame *debug_frame;

	void (*keypoint_estimation_run_func)(void *);

	u_frame_times_widget ft_widget;

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

} // namespace xrt::tracking::hand::mercury
