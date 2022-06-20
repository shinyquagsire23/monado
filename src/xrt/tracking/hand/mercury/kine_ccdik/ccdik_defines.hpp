// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Defines for kinematic model
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */


#pragma once
#include "math/m_api.h"


#include <stddef.h>
#include <unistd.h>

#include <Eigen/Core>
#include <Eigen/Geometry>

#include "math/m_eigen_interop.hpp"
#include "math/m_api.h"
#include "math/m_space.h"
#include "math/m_vec3.h"

#include "util/u_trace_marker.h"

#include "os/os_time.h"
#include "util/u_time.h"

#include <iostream>
#include <vector>

#include <stdio.h>

#include <stddef.h>
#include <unistd.h>
#include "../kine_common.hpp"


using namespace xrt::auxiliary::math;

namespace xrt::tracking::hand::mercury::ccdik {

enum class HandJoint26KP : int
{
	PALM = 0,
	WRIST = 1,

	THUMB_METACARPAL = 2,
	THUMB_PROXIMAL = 3,
	THUMB_DISTAL = 4,
	THUMB_TIP = 5,

	INDEX_METACARPAL = 6,
	INDEX_PROXIMAL = 7,
	INDEX_INTERMEDIATE = 8,
	INDEX_DISTAL = 9,
	INDEX_TIP = 10,

	MIDDLE_METACARPAL = 11,
	MIDDLE_PROXIMAL = 12,
	MIDDLE_INTERMEDIATE = 13,
	MIDDLE_DISTAL = 14,
	MIDDLE_TIP = 15,

	RING_METACARPAL = 16,
	RING_PROXIMAL = 17,
	RING_INTERMEDIATE = 18,
	RING_DISTAL = 19,
	RING_TIP = 20,

	LITTLE_METACARPAL = 21,
	LITTLE_PROXIMAL = 22,
	LITTLE_INTERMEDIATE = 23,
	LITTLE_DISTAL = 24,
	LITTLE_TIP = 25,
};

enum HandFinger
{
	HF_THUMB = 0,
	HF_INDEX = 1,
	HF_MIDDLE = 2,
	HF_RING = 3,
	HF_LITTLE = 4,
};

enum FingerBone
{
	// FB_CARPAL,
	FB_METACARPAL,
	FB_PROXIMAL,
	FB_INTERMEDIATE,
	FB_DISTAL
};

enum ThumbBone
{
	// TB_CARPAL,
	TB_METACARPAL,
	TB_PROXIMAL,
	TB_DISTAL
};

const size_t kNumNNJoints = 21;

struct wct_t
{
	float waggle = 0.0f;
	float curl = 0.0f;
	float twist = 0.0f;
};

// IGNORE THE FIRST BONE in the wrist.
struct bone_t
{
	// EIGEN_OVERRIDE_OPERATOR_NEW
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	// will always be 0, 0, -(some amount) for mcp, pxm, int, dst
	// will be random amounts for carpal bones
	Eigen::Vector3f trans_from_last_joint = Eigen::Vector3f::Zero();
	wct_t rot_to_next_joint_wct = {};
	Eigen::Quaternionf rot_to_next_joint_quat = {};
	// Translation from last joint to this joint, rotation that takes last joint's -z and points it at next joint
	Eigen::Affine3f bone_relation = {};

	// Imagine it like transforming an object at the origin to this bone's position/orientation.
	Eigen::Affine3f world_pose = {};

	struct
	{
		Eigen::Affine3f *world_pose;
		Eigen::Affine3f *bone_relation;
	} parent;


	wct_t joint_limit_min = {};
	wct_t joint_limit_max = {};


	// What keypoint out of the ML model does this correspond to?
	Joint21::Joint21 keypoint_idx_21 = {};
};

// translation: wrist to mcp (-z and x). rotation: from wrist space to metacarpal space
// translation: mcp to pxm (just -z). rotation: from mcp to pxm space

struct finger_t
{
	bone_t bones[5] = {};
};


struct KinematicHandCCDIK
{
	// The distance from the wrist to the middle-proximal joint - this sets the overall hand size.
	float size;
	bool is_right;
	xrt_pose right_in_left;

	// Wrist pose, scaled by size.
	Eigen::Affine3f wrist_relation = {};

	finger_t fingers[5] = {};

	xrt_vec3 t_jts[kNumNNJoints] = {};
	Eigen::Matrix<float, 3, kNumNNJoints> t_jts_as_mat = {};
	Eigen::Matrix<float, 3, kNumNNJoints> kinematic = {};
};


#define CONTINUE_IF_HIDDEN_THUMB                                                                                       \
	if (finger_idx == 0 && bone_idx == 0) {                                                                        \
		continue;                                                                                              \
	}

} // namespace xrt::tracking::hand::mercury::ccdik