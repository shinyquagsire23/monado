// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Interface for Cyclic Coordinate Descent IK kinematic optimizer
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */
#pragma once
// #include "math/m_api.h"

#include "xrt/xrt_defines.h"
#include "../kine_common.hpp"

namespace xrt::tracking::hand::mercury::ccdik {

struct KinematicHandCCDIK;

void
alloc_kinematic_hand(xrt_pose left_in_right, bool is_right, KinematicHandCCDIK **out_kinematic_hand);

void
optimize_new_frame(KinematicHandCCDIK *hand, one_frame_input &observation, struct xrt_hand_joint_set &out_set);

void
init_hardcoded_statics(KinematicHandCCDIK *hand, float size = 0.095864);

void
free_kinematic_hand(KinematicHandCCDIK **kinematic_hand);

} // namespace xrt::tracking::hand::mercury::ccdik
