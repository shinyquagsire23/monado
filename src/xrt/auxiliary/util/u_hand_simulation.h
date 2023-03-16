// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Wrapper around Mercury's parametric hand code, used by Index and OpenGloves to simulate hand tracking.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_defines.h"

#include "util/u_misc.h"
#include "util/u_hand_tracking.h"


#ifdef __cplusplus
extern "C" {
#endif

struct u_hand_sim_metacarpal
{
	struct xrt_vec2 swing;
	float twist;
};

struct u_hand_sim_finger
{
	struct u_hand_sim_metacarpal metacarpal;
	struct xrt_vec2 proximal_swing;
	// rotation at intermediate joint, then rotation at distal joint
	float rotations[2];
};

struct u_hand_sim_thumb
{
	struct u_hand_sim_metacarpal metacarpal;
	float rotations[2];
};

struct u_hand_sim_hand
{
	// Distance from wrist to middle-proximal.
	bool is_right;
	float hand_size;
	struct xrt_space_relation wrist_pose;
	struct xrt_space_relation hand_pose;

	struct u_hand_sim_thumb thumb;
	struct u_hand_sim_finger finger[4];
};


void
u_hand_sim_simulate(struct u_hand_sim_hand *hand, struct xrt_hand_joint_set *out_set);

void
u_hand_sim_simulate_for_valve_index_knuckles(const struct u_hand_tracking_curl_values *values,
                                             enum xrt_hand xhand,
                                             const struct xrt_space_relation *root_pose,
                                             struct xrt_hand_joint_set *out_set);

void
u_hand_sim_simulate_generic(const struct u_hand_tracking_values *values,
                            enum xrt_hand xhand,
                            const struct xrt_space_relation *root_pose,
                            struct xrt_hand_joint_set *out_set);

#ifdef __cplusplus
}
#endif
