// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Levenberg-Marquardt kinematic optimizer
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */

#pragma once
#include "math/m_api.h"
#include "math/m_vec3.h"

#include "lm_defines.hpp"


namespace xrt::tracking::hand::mercury::lm {

bool
hand_init_guess(one_frame_input &observation, const float hand_size, xrt_pose left_in_right, xrt_pose &out_wrist_guess);

} // namespace xrt::tracking::hand::mercury::lm
