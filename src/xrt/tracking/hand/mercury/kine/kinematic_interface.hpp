// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Interface for kinematic model
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */
#pragma once
#include "math/m_api.h"

#include "xrt/xrt_defines.h"


namespace xrt::tracking::hand::mercury::kine {

struct kinematic_hand_4f;

void
alloc_kinematic_hand(kinematic_hand_4f **out_kinematic_hand);

void
optimize_new_frame(xrt_vec3 model_joints_3d[21],
                   kinematic_hand_4f *hand,
                   struct xrt_hand_joint_set *out_set,
                   int hand_idx);

void
init_hardcoded_statics(kinematic_hand_4f *hand, float size = 0.095864);

void
free_kinematic_hand(kinematic_hand_4f **kinematic_hand);

} // namespace xrt::tracking::hand::mercury::kine
