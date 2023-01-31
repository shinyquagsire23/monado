// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Interface for Levenberg-Marquardt kinematic optimizer
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */
#pragma once
#include "xrt/xrt_defines.h"
#include "util/u_logging.h"
// #include "lm_defines.hpp"
#include "../kine_common.hpp"

namespace xrt::tracking::hand::mercury::lm {

// Yes, this is a weird in-between-C-and-C++ API. Fight me, I like it this way.

// Opaque struct.
struct KinematicHandLM;

// Constructor
void
optimizer_create(xrt_pose left_in_right,
                 bool is_right,
                 u_logging_level log_level,
                 KinematicHandLM **out_kinematic_hand);

/*!
 * The main tracking code calls this function with some 2D(ish) camera observations of the hand, and this function
 * calculates a good 3D hand pose and writes it to out_viz_hand.
 *
 * @param observation The observation of the hand joints. Warning, this function will mutate the observation
 * unpredictably. Keep a copy of it if you need it after.
 * @param hand_was_untracked_last_frame: If the hand was untracked last frame (it was out of view, obscured, ML models
 * failed, etc.) - if it was, we don't want to enforce temporal consistency because we have no good previous hand state
 * with which to do that.
 * @param optimize_hand_size: Whether or not it's allowed to tweak the hand size - when we're calibrating the user's
 * hand size, we want to do that; afterwards we don't want to waste the compute.
 * @param target_hand_size: The hand size we want it to get close to
 * @param hand_size_err_mul: A multiplier to help determine how close it has to get to that hand size
 * @param[out] out_hand: The xrt_hand_joint_set to output its result to
 * @param[out] out_hand_size: The hand size it ended up at
 * @param[out] out_reprojection_error: The reprojection error it ended up at
 */

void
optimizer_run(KinematicHandLM *hand,
              one_frame_input &observation,
              bool hand_was_untracked_last_frame,
              float smoothing_factor, //!<- Unused if this is the first frame
              bool optimize_hand_size,
              float target_hand_size,
              float hand_size_err_mul,
              float amt_use_depth,
              xrt_hand_joint_set &out_hand,
              float &out_hand_size,
              float &out_reprojection_error);

// Destructor
void
optimizer_destroy(KinematicHandLM **hand);

} // namespace xrt::tracking::hand::mercury::lm
