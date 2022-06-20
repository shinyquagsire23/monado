// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Setter-upper for kinematic model
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */


#pragma once

#include "ccdik_defines.hpp"
#include "ccdik_tiny_math.hpp"


namespace xrt::tracking::hand::mercury::ccdik {

void
bone_update_quat_and_matrix(struct bone_t *bone)
{
	wct_to_quat(bone->rot_to_next_joint_wct, (xrt_quat *)&bone->rot_to_next_joint_quat);

	// To make sure that the bottom-right cell is 1.0f. You can do this somewhere else (just once) for speed
	bone->bone_relation.setIdentity();
	bone->bone_relation.translation() = bone->trans_from_last_joint;
	bone->bone_relation.linear() = Eigen::Matrix3f(bone->rot_to_next_joint_quat);
}

void
eval_chain(std::vector<const Eigen::Affine3f *> &chain, Eigen::Affine3f &out)
{
	out.setIdentity();
	for (const Eigen::Affine3f *new_matrix : chain) {
		// I have NO IDEA if it's correct to left-multiply or right-multiply.
		// I need to go to school for linear algebra for a lot longer...
		out = out * (*new_matrix);
	}
}

void
_statics_init_world_parents(KinematicHandCCDIK *hand)
{
	for (int finger = 0; finger < 5; finger++) {
		finger_t *of = &hand->fingers[finger];
		of->bones[0].parent.world_pose = &hand->wrist_relation;

		// Does this make any sense? Be careful here...
		of->bones[0].parent.bone_relation = &hand->wrist_relation;

		for (int bone = 1; bone < 5; bone++) {
			of->bones[bone].parent.world_pose = &of->bones[bone - 1].world_pose;
			of->bones[bone].parent.bone_relation = &of->bones[bone - 1].bone_relation;
		}
	}
}

void
_statics_init_world_poses(KinematicHandCCDIK *hand)
{
	XRT_TRACE_MARKER();
	for (int finger = 0; finger < 5; finger++) {
		finger_t *of = &hand->fingers[finger];

		for (int bone = 0; bone < 5; bone++) {
			of->bones[bone].world_pose =
			    (*of->bones[bone].parent.world_pose) * of->bones[bone].bone_relation;
		}
	}
}

void
_statics_init_loc_ptrs(KinematicHandCCDIK *hand)
{
	hand->fingers[0].bones[1].keypoint_idx_21 = Joint21::THMB_MCP;
	hand->fingers[0].bones[2].keypoint_idx_21 = Joint21::THMB_PXM;
	hand->fingers[0].bones[3].keypoint_idx_21 = Joint21::THMB_DST;
	hand->fingers[0].bones[4].keypoint_idx_21 = Joint21::THMB_TIP;

	hand->fingers[1].bones[1].keypoint_idx_21 = Joint21::INDX_PXM;
	hand->fingers[1].bones[2].keypoint_idx_21 = Joint21::INDX_INT;
	hand->fingers[1].bones[3].keypoint_idx_21 = Joint21::INDX_DST;
	hand->fingers[1].bones[4].keypoint_idx_21 = Joint21::INDX_TIP;

	hand->fingers[2].bones[1].keypoint_idx_21 = Joint21::MIDL_PXM;
	hand->fingers[2].bones[2].keypoint_idx_21 = Joint21::MIDL_INT;
	hand->fingers[2].bones[3].keypoint_idx_21 = Joint21::MIDL_DST;
	hand->fingers[2].bones[4].keypoint_idx_21 = Joint21::MIDL_TIP;

	hand->fingers[3].bones[1].keypoint_idx_21 = Joint21::RING_PXM;
	hand->fingers[3].bones[2].keypoint_idx_21 = Joint21::RING_INT;
	hand->fingers[3].bones[3].keypoint_idx_21 = Joint21::RING_DST;
	hand->fingers[3].bones[4].keypoint_idx_21 = Joint21::RING_TIP;

	hand->fingers[4].bones[1].keypoint_idx_21 = Joint21::LITL_PXM;
	hand->fingers[4].bones[2].keypoint_idx_21 = Joint21::LITL_INT;
	hand->fingers[4].bones[3].keypoint_idx_21 = Joint21::LITL_DST;
	hand->fingers[4].bones[4].keypoint_idx_21 = Joint21::LITL_TIP;
}

void
_statics_joint_limits(KinematicHandCCDIK *hand)
{
	{
		finger_t *t = &hand->fingers[0];
		t->bones[1].joint_limit_max.waggle = rad(70);
		t->bones[1].joint_limit_min.waggle = rad(-70);

		t->bones[1].joint_limit_max.curl = rad(70);
		t->bones[1].joint_limit_min.curl = rad(-70);

		t->bones[1].joint_limit_max.twist = rad(40);
		t->bones[1].joint_limit_min.twist = rad(-40);

		//

		t->bones[2].joint_limit_max.waggle = rad(0);
		t->bones[2].joint_limit_min.waggle = rad(0);

		t->bones[2].joint_limit_max.curl = rad(50);
		t->bones[2].joint_limit_min.curl = rad(-100);

		t->bones[2].joint_limit_max.twist = rad(0);
		t->bones[2].joint_limit_min.twist = rad(0);

		//

		t->bones[3].joint_limit_max.waggle = rad(0);
		t->bones[3].joint_limit_min.waggle = rad(0);

		t->bones[3].joint_limit_max.curl = rad(50);
		t->bones[3].joint_limit_min.curl = rad(-100);

		t->bones[3].joint_limit_max.twist = rad(0);
		t->bones[3].joint_limit_min.twist = rad(0);
	}
}

// Exported:
void
init_hardcoded_statics(KinematicHandCCDIK *hand, float size)
{
	hand->size = size;
	hand->wrist_relation.setIdentity();
	hand->wrist_relation.linear() *= hand->size;

	{

		finger_t *t = &hand->fingers[0];

		// Hidden extra bone that makes our code easier to write. Note the weird extra rotation.
		t->bones[0].rot_to_next_joint_wct = {-rad(45), rad(-10), -rad(70)};
		t->bones[0].trans_from_last_joint = {0.33097, 0, -0.25968};

		t->bones[1].rot_to_next_joint_wct = {0, rad(-5), 0};

		t->bones[2].rot_to_next_joint_wct = {0, rad(-25), 0};
		t->bones[2].trans_from_last_joint.z() = -0.389626;

		t->bones[3].rot_to_next_joint_wct = {0, rad(-25), 0};
		t->bones[3].trans_from_last_joint.z() = -0.311176;

		t->bones[4].trans_from_last_joint.z() = -0.232195;
	}


	float wagg = -0.19;

	float finger_joints[4][3] = {
	    {
	        -0.365719,
	        -0.231581,
	        -0.201790,
	    },
	    {
	        -0.404486,
	        -0.247749,
	        -0.210121,
	    },
	    {
	        -0.365639,
	        -0.225666,
	        -0.187089,
	    },
	    {
	        -0.278197,
	        -0.176178,
	        -0.157566,
	    },
	};

	for (int finger = HF_INDEX; finger <= HF_LITTLE; finger++) {
		finger_t *of = &hand->fingers[finger];
		of->bones[0].rot_to_next_joint_wct.waggle = wagg;
		wagg += 0.19f;

		of->bones[FB_PROXIMAL].rot_to_next_joint_wct.curl = rad(-5);
		of->bones[FB_INTERMEDIATE].rot_to_next_joint_wct.curl = rad(-5);
		of->bones[FB_DISTAL].rot_to_next_joint_wct.curl = rad(-5);


		for (int i = 0; i < 3; i++) {
			int bone = i + 2;
			of->bones[bone].trans_from_last_joint.x() = 0;
			of->bones[bone].trans_from_last_joint.y() = 0;
			of->bones[bone].trans_from_last_joint.z() = finger_joints[finger - 1][i];
		}
	}


	hand->fingers[1].bones[1].trans_from_last_joint.z() = -0.66;
	hand->fingers[2].bones[1].trans_from_last_joint.z() = -0.645;
	hand->fingers[3].bones[1].trans_from_last_joint.z() = -0.58;
	hand->fingers[4].bones[1].trans_from_last_joint.z() = -0.52;

	hand->fingers[HF_INDEX].bones[0].trans_from_last_joint = {0.16926, 0, -0.34437};
	hand->fingers[HF_MIDDLE].bones[0].trans_from_last_joint = {0.034639, 0, -0.35573};
	hand->fingers[HF_RING].bones[0].trans_from_last_joint = {-0.063625, 0, -0.34164};
	hand->fingers[HF_LITTLE].bones[0].trans_from_last_joint = {-0.1509, 0, -0.30373};

	for (int finger_idx = 0; finger_idx < 5; finger_idx++) {
		for (int bone_idx = 0; bone_idx < 5; bone_idx++) {
			bone_update_quat_and_matrix(&hand->fingers[finger_idx].bones[bone_idx]);
		}
	}

	_statics_init_world_parents(hand);
	_statics_init_world_poses(hand);
	_statics_init_loc_ptrs(hand);
	_statics_joint_limits(hand);
}
} // namespace xrt::tracking::hand::mercury::ccdik
