// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Debug instrumentation for mercury_train or others to control hand tracking.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */

#pragma once

#include "hg_interface.h"

#include "util/u_var.h"


#ifdef __cplusplus
namespace xrt::tracking::hand::mercury {
extern "C" {
#endif

struct hg_tuneable_values
{
	bool new_user_event = false;
	struct u_var_draggable_f32 after_detection_fac;
	struct u_var_draggable_f32 dyn_radii_fac;
	struct u_var_draggable_f32 dyn_joint_y_angle_error;
	struct u_var_draggable_f32 amount_to_lerp_prediction;
	struct u_var_draggable_f32 amt_use_depth;
	struct u_var_draggable_f32 mpiou_any;
	struct u_var_draggable_f32 mpiou_single_detection;
	struct u_var_draggable_f32 mpiou_double_detection;
	struct u_var_draggable_f32 max_reprojection_error;
	struct u_var_draggable_f32 opt_smooth_factor;
	struct u_var_draggable_f32 max_hand_dist;
	bool scribble_predictions_into_next_frame = false;
	bool scribble_keypoint_model_outputs = false;
	bool scribble_optimizer_outputs = true;
	bool always_run_detection_model = false;
	bool optimize_hand_size = true;
	int max_num_outside_view = 6;
	size_t num_frames_before_display = 10;
	bool enable_pose_predicted_input = true;
	bool enable_framerate_based_smoothing = false;

	// Stuff that's only really useful for dataset playback:
	bool detection_model_in_both_views = false;
};

struct hg_tuneable_values *
t_hand_tracking_sync_mercury_get_tuneable_values_pointer(struct t_hand_tracking_sync *ht_sync);

#ifdef __cplusplus
}
} // namespace xrt::tracking::hand::mercury
#endif
