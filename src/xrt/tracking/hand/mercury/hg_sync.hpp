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
#include "hg_debug_instrumentation.hpp"

#include "tracking/t_hand_tracking.h"
#include "tracking/t_camera_models.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"

#include "math/m_api.h"
#include "math/m_vec2.h"
#include "math/m_vec3.h"
#include "math/m_mathinclude.h"
#include "math/m_eigen_interop.hpp"

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
#include <onnxruntime_c_api.h>

#include "kine_common.hpp"
#include "kine_lm/lm_interface.hpp"


namespace xrt::tracking::hand::mercury {

using namespace xrt::auxiliary::util;
using namespace xrt::auxiliary::math;

#define HG_TRACE(hgt, ...) U_LOG_IFL_T(hgt->log_level, __VA_ARGS__)
#define HG_DEBUG(hgt, ...) U_LOG_IFL_D(hgt->log_level, __VA_ARGS__)
#define HG_INFO(hgt, ...) U_LOG_IFL_I(hgt->log_level, __VA_ARGS__)
#define HG_WARN(hgt, ...) U_LOG_IFL_W(hgt->log_level, __VA_ARGS__)
#define HG_ERROR(hgt, ...) U_LOG_IFL_E(hgt->log_level, __VA_ARGS__)

static constexpr uint16_t kDetectionInputSize = 160;
static constexpr uint16_t kKeypointInputSize = 128;

static constexpr uint16_t kKeypointOutputHeatmapSize = 22;
static constexpr uint16_t kVisSpacerSize = 8;

static const cv::Scalar RED(255, 30, 30);
static const cv::Scalar YELLOW(255, 255, 0);
static const cv::Scalar PINK(255, 0, 255);
static const cv::Scalar GREEN(0, 255, 0);

static const cv::Scalar colors[2] = {YELLOW, RED};

constexpr enum xrt_hand_joint joints_5x5_to_26[5][5] = {
    {
        XRT_HAND_JOINT_WRIST,
        XRT_HAND_JOINT_THUMB_METACARPAL,
        XRT_HAND_JOINT_THUMB_PROXIMAL,
        XRT_HAND_JOINT_THUMB_DISTAL,
        XRT_HAND_JOINT_THUMB_TIP,
    },
    {
        XRT_HAND_JOINT_INDEX_METACARPAL,
        XRT_HAND_JOINT_INDEX_PROXIMAL,
        XRT_HAND_JOINT_INDEX_INTERMEDIATE,
        XRT_HAND_JOINT_INDEX_DISTAL,
        XRT_HAND_JOINT_INDEX_TIP,
    },
    {
        XRT_HAND_JOINT_MIDDLE_METACARPAL,
        XRT_HAND_JOINT_MIDDLE_PROXIMAL,
        XRT_HAND_JOINT_MIDDLE_INTERMEDIATE,
        XRT_HAND_JOINT_MIDDLE_DISTAL,
        XRT_HAND_JOINT_MIDDLE_TIP,
    },
    {
        XRT_HAND_JOINT_RING_METACARPAL,
        XRT_HAND_JOINT_RING_PROXIMAL,
        XRT_HAND_JOINT_RING_INTERMEDIATE,
        XRT_HAND_JOINT_RING_DISTAL,
        XRT_HAND_JOINT_RING_TIP,
    },
    {
        XRT_HAND_JOINT_LITTLE_METACARPAL,
        XRT_HAND_JOINT_LITTLE_PROXIMAL,
        XRT_HAND_JOINT_LITTLE_INTERMEDIATE,
        XRT_HAND_JOINT_LITTLE_DISTAL,
        XRT_HAND_JOINT_LITTLE_TIP,
    },
};

namespace ROIProvenance {
	enum ROIProvenance
	{
		HAND_DETECTION,
		POSE_PREDICTION
	};
}


// Forward declaration for ht_view
struct HandTracking;
struct ht_view;


struct Hand3D
{
	struct xrt_vec3 kps[21];
};

using hand21_2d = std::array<vec2_5, 21>;

struct projection_instructions
{
	Eigen::Quaternionf rot_quat = Eigen::Quaternionf::Identity();
	float stereographic_radius = 0;
	bool flip = false;
	const t_camera_model_params &dist;

	projection_instructions(const t_camera_model_params &dist) : dist(dist) {}
};

struct model_input_wrap
{
	float *data = nullptr;
	int64_t dimensions[4];
	size_t num_dimensions = 0;

	OrtValue *tensor = nullptr;
	const char *name;
};

struct onnx_wrap
{
	const OrtApi *api = nullptr;
	OrtEnv *env = nullptr;

	OrtMemoryInfo *meminfo = nullptr;
	OrtSession *session = nullptr;

	std::vector<model_input_wrap> wraps = {};
};

// Multipurpose.
// * Hand detector writes into center_px, size_px, found and hand_detection_confidence
// * Keypoint estimator operates on this to a direction/radius for the stereographic projection, and for the associated
// keypoints.
struct hand_region_of_interest
{
	ROIProvenance::ROIProvenance provenance;

	// Either set by the detection model or by predict_new_regions_of_interest/back_project
	xrt_vec2 center_px;
	float size_px;

	bool found;
	bool hand_detection_confidence;
};



struct hand_detection_run_info
{
	ht_view *view;
	// These are not duplicates of ht_view's regions_of_interest_this_frame!
	// If some hands are already tracked, we have logic that only copies new ROIs to this frame's regions of
	// interest.
	hand_region_of_interest outputs[2];
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

	t_camera_model_params hgdist_orig;
	// With fx, fy, cx, cy scaled to the current camera resolution as appropriate.
	t_camera_model_params hgdist;


	cv::Mat run_model_on_this;
	cv::Mat debug_out_to_this;

	struct hand_region_of_interest regions_of_interest_this_frame[2]; // left, right

	struct keypoint_estimation_run_info run_info[2];
};


struct hand_size_refinement
{
	int num_hands;
	float out_hand_size;
	float out_hand_confidence;
	float hand_size_refinement_schedule_x = 0;
	float hand_size_refinement_schedule_y = 0;
	bool optimizing = true;
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


	struct t_stereo_camera_calibration *calib;

	struct xrt_size calibration_one_view_size_px = {};

	// So that we can calibrate cameras at 1280x800 but ship images over USB at 640x400
	struct xrt_size last_frame_one_view_size_px = {};

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

	// These are produced by the keypoint estimator and consumed by the nonlinear optimizer
	// left hand, right hand THEN left view, right view
	struct one_frame_input keypoint_outputs[2];

	// Used to track whether this hand has *ever* been seen during this user's session, so that we can spend some
	// extra time optimizing their hand size if one of their hands isn't visible for the first bit.
	bool hand_seen_before[2] = {false, false};

	// Used to:
	// * see if a hand is currently being tracked.
	// * If so, don't replace the bounding box with that from a hand detection.
	// * Also, if both hands are being tracked, we just don't run the hand detector.
	bool last_frame_hand_detected[2] = {false, false};

	// Used to decide whether to run the keypoint estimator/nonlinear optimizer.
	bool this_frame_hand_detected[2] = {false, false};

	// Used to determine pose-predicted regions of interest. Contains the last 2 hand keypoint positions, or less
	// if the hand has just started being tracked.
	HistoryBuffer<Eigen::Array<float, 3, 21>, 2> history_hands[2] = {};

	// Contains the last 2 timestamps, or less if hand tracking has just started.
	HistoryBuffer<uint64_t, 2> history_timestamps = {};

	// It'd be a staring contest between your hand and the heat death of the universe!
	uint64_t hand_tracked_for_num_frames[2] = {0, 0};


	// left hand, right hand
	Eigen::Array<float, 3, 21> pose_predicted_keypoints[2];

	int detection_counter = 0;

	struct hand_size_refinement refinement = {};
	float target_hand_size = STANDARD_HAND_SIZE;


	xrt_frame *debug_frame;


	// This should be removed.
	void (*keypoint_estimation_run_func)(void *);



	struct xrt_pose left_in_right = {};

	u_frame_times_widget ft_widget = {};

	struct hg_tuneable_values tuneable_values;

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
release_onnx_wrap(onnx_wrap *wrap);


void
make_projection_instructions(t_camera_model_params &dist,
                             bool flip_after,
                             float expand_val,
                             float twist,
                             Eigen::Array<float, 3, 21> &joints,
                             projection_instructions &out_instructions,
                             hand21_2d &out_hand);


void
make_projection_instructions_angular(xrt_vec3 direction_3d,
                                     bool flip_after,
                                     float angular_radius,
                                     float expand_val,
                                     float twist,
                                     projection_instructions &out_instructions);

void
stereographic_project_image(const t_camera_model_params &dist,
                            const projection_instructions &instructions,
                            cv::Mat &input_image,
                            cv::Mat *debug_image,
                            const cv::Scalar boundary_color,
                            cv::Mat &out);



} // namespace xrt::tracking::hand::mercury
