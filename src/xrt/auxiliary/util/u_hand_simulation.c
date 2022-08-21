// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Wrapper around Mercury's parametric hand code, used by Index and OpenGloves to simulate hand tracking.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup aux_util
 */

#include "math/m_mathinclude.h"
#include "util/u_hand_tracking.h"
#include "xrt/xrt_defines.h"
#include "u_hand_simulation.h"
#include "math/m_api.h"
#include "u_trace_marker.h"

#define HAND_SIM_NUM_FINGERS 5

// This is a lie for the thumb; we usually do the hidden metacarpal trick there
#define HAND_SIM_NUM_JOINTS_IN_FINGER 5
#define HAND_SIM_NUM_ORIENTATIONS_IN_FINGER 4

struct translations55
{
	struct xrt_vec3 t[HAND_SIM_NUM_FINGERS][HAND_SIM_NUM_JOINTS_IN_FINGER];
};

struct orientations54
{
	struct xrt_quat q[HAND_SIM_NUM_FINGERS][HAND_SIM_NUM_ORIENTATIONS_IN_FINGER];
};

#define DEG_TO_RAD(DEG) (DEG * M_PI / 180.)

// For debugging.
#if 0
#include <iostream>
#define assert_quat_length_1(q)                                                                                        \
	{                                                                                                              \
		const T scale = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];                                 \
		if (abs(scale - T(1.0)) > 0.001) {                                                                     \
			std::cout << "Length bad! " << scale << std::endl;                                             \
			assert(false);                                                                                 \
		};                                                                                                     \
	}
#else
#define assert_quat_length_1(q)
#endif

static void
eval_hand_set_rel_orientations(const struct u_hand_sim_hand *opt, struct orientations54 *rel_orientations)
{

// Thumb MCP hidden orientation
#if 0
	Vec2<T> mcp_root_swing;

	mcp_root_swing.x = rad<T>((T)(-10));
	mcp_root_swing.y = rad<T>((T)(-40));

	T mcp_root_twist = rad<T>((T)(-80));

	SwingTwistToQuaternion(mcp_root_swing, mcp_root_twist, rel_orientations.q[0][0]);

	std::cout << "\n\n\n\nHIDDEN ORIENTATION\n";
	std::cout << std::setprecision(100);
	std::cout << rel_orientations.q[0][0].w << std::endl;
	std::cout << rel_orientations.q[0][0].x << std::endl;
	std::cout << rel_orientations.q[0][0].y << std::endl;
	std::cout << rel_orientations.q[0][0].z << std::endl;
#else
	// This should be exactly equivalent to the above
	rel_orientations->q[0][0].w = 0.716990172863006591796875f;
	rel_orientations->q[0][0].x = 0.1541481912136077880859375f;
	rel_orientations->q[0][0].y = -0.31655871868133544921875f;
	rel_orientations->q[0][0].z = -0.6016261577606201171875f;
#endif

	// Thumb MCP orientation
	math_quat_from_swing_twist(&opt->thumb.metacarpal.swing, //
	                           opt->thumb.metacarpal.twist,  //
	                           &rel_orientations->q[0][1]);

	// Thumb curls
	struct xrt_vec2 thumb_swing0 = {opt->thumb.rotations[0], 0.f};
	math_quat_from_swing(&thumb_swing0, &rel_orientations->q[0][2]);

	struct xrt_vec2 thumb_swing1 = {opt->thumb.rotations[1], 0.f};
	math_quat_from_swing(&thumb_swing1, &rel_orientations->q[0][3]);

	// Finger orientations
	for (int i = 0; i < 4; i++) {
		math_quat_from_swing_twist(&opt->finger[i].metacarpal.swing, //
		                           opt->finger[i].metacarpal.twist,  //
		                           &rel_orientations->q[i + 1][0]);

		math_quat_from_swing(&opt->finger[i].proximal_swing, //
		                     &rel_orientations->q[i + 1][1]);

		struct xrt_vec2 finger_swing0 = {opt->finger[i].rotations[0], 0.f};
		math_quat_from_swing(&finger_swing0, &rel_orientations->q[i + 1][2]);
		struct xrt_vec2 finger_swing1 = {opt->finger[i].rotations[1], 0.f};
		math_quat_from_swing(&finger_swing1, &rel_orientations->q[i + 1][3]);
	}
}

static inline void
eval_hand_set_rel_translations(const struct u_hand_sim_hand *opt, struct translations55 *rel_translations)
{
	// Basically, we're walking up rel_translations, writing strictly sequentially. Hopefully this is fast.


	// Thumb metacarpal translation.
	rel_translations->t[0][0] = (struct xrt_vec3){0.33097f, -0.1f, -0.25968f};

	// Comes after the invisible joint.
	rel_translations->t[0][1] = (struct xrt_vec3){0.f, 0.f, 0.f};
	// prox, distal, tip
	rel_translations->t[0][2] = (struct xrt_vec3){0.f, 0.f, -0.389626f};
	rel_translations->t[0][3] = (struct xrt_vec3){0.f, 0.f, -0.311176f};
	rel_translations->t[0][4] = (struct xrt_vec3){0.f, 0.f, -0.232195f};

	// What's the best place to put this? Here works, but is there somewhere we could put it where it gets accessed
	// faster?
	float finger_joint_lengths[4][4] = {
	    {
	        -0.66f,
	        -0.365719f,
	        -0.231581f,
	        -0.201790f,
	    },
	    {
	        -0.645f,
	        -0.404486f,
	        -0.247749f,
	        -0.210121f,
	    },
	    {
	        -0.58f,
	        -0.365639f,
	        -0.225666f,
	        -0.187089f,
	    },
	    {
	        -0.52f,
	        -0.278197f,
	        -0.176178f,
	        -0.157566f,
	    },
	};

	// Index metacarpal
	rel_translations->t[1][0] = (struct xrt_vec3){0.16926f, 0.f, -0.34437f};
	// Middle
	rel_translations->t[2][0] = (struct xrt_vec3){0.034639f, 0.01f, -0.35573f};
	// Ring
	rel_translations->t[3][0] = (struct xrt_vec3){-0.063625f, 0.005f, -0.34164f};
	// Little
	rel_translations->t[4][0] = (struct xrt_vec3){-0.1509f, -0.005f, -0.30373f};

	// Index to little finger
	for (int finger = 0; finger < 4; finger++) {
		for (int i = 0; i < 4; i++) {
			int bone = i + 1;
			rel_translations->t[finger + 1][bone].x = 0.f;
			rel_translations->t[finger + 1][bone].y = 0.f;
			rel_translations->t[finger + 1][bone].z = finger_joint_lengths[finger][i];
		}
	}
}

void
eval_hand_with_orientation(const struct u_hand_sim_hand *opt,
                           bool is_right,
                           struct translations55 *translations_absolute,
                           struct orientations54 *orientations_absolute)

{
	XRT_TRACE_MARKER();

	struct translations55 rel_translations;
	struct orientations54 rel_orientations;

	eval_hand_set_rel_orientations(opt, &rel_orientations);

	eval_hand_set_rel_translations(opt, &rel_translations);

	struct xrt_quat orientation_root = XRT_QUAT_IDENTITY;

	// Get each joint's tracking-relative orientation by rotating its parent-relative orientation by the
	// tracking-relative orientation of its parent.
	for (size_t finger = 0; finger < HAND_SIM_NUM_FINGERS; finger++) {
		struct xrt_quat *last_orientation = &orientation_root;
		for (size_t bone = 0; bone < HAND_SIM_NUM_ORIENTATIONS_IN_FINGER; bone++) {
			struct xrt_quat *rel_orientation = &rel_orientations.q[finger][bone];
			struct xrt_quat *out_orientation = &orientations_absolute->q[finger][bone];

			math_quat_rotate(last_orientation, rel_orientation, out_orientation);
			last_orientation = out_orientation;
		}
	}

	// Get each joint's tracking-relative position by rotating its parent-relative translation by the
	// tracking-relative orientation of its parent, then adding that to its parent's tracking-relative position.
	struct xrt_vec3 zero = XRT_VEC3_ZERO;
	for (size_t finger = 0; finger < HAND_SIM_NUM_FINGERS; finger++) {
		const struct xrt_vec3 *last_translation = &zero;
		const struct xrt_quat *last_orientation = &orientation_root;
		for (size_t bone = 0; bone < HAND_SIM_NUM_JOINTS_IN_FINGER; bone++) {
			struct xrt_vec3 *out_translation = &translations_absolute->t[finger][bone];
			struct xrt_vec3 *rel_translation = &rel_translations.t[finger][bone];

			// rotate and scale
			math_quat_rotate_vec3(last_orientation, rel_translation, out_translation);
			math_vec3_scalar_mul(opt->hand_size, out_translation);

			// If this is a right hand, mirror it.
			if (is_right) {
				out_translation->x *= -1;
			}

			out_translation->x += last_translation->x;
			out_translation->y += last_translation->y;
			out_translation->z += last_translation->z;

			// Next iteration, the orientation to rotate by should be the tracking-relative orientation of
			// this joint.

			// If bone < 4 so we don't go over the end of orientations_absolute. I hope this gets optimized
			// out anyway.
			if (bone < 4) {
				last_orientation = &orientations_absolute->q[finger][bone];
				// Ditto for translation
				last_translation = out_translation;
			}
		}
	}
}

static inline void
zldtt_ori_right(const struct xrt_quat *orientation, struct xrt_quat *out)
{
	struct xrt_quat tmp;
	tmp.w = orientation->w;
	tmp.x = orientation->x;
	tmp.y = orientation->y;
	tmp.z = orientation->z;

	struct xrt_vec3 x = XRT_VEC3_UNIT_X;
	struct xrt_vec3 z = XRT_VEC3_UNIT_Z;

	math_quat_rotate_vec3(&tmp, &x, &x);
	math_quat_rotate_vec3(&tmp, &z, &z);

	// This is a very squashed change-of-basis from left-handed coordinate systems to right-handed coordinate
	// systems: you multiply everything by (-1 0 0) then negate the X axis.

	x.y *= -1;
	x.z *= -1;

	z.x *= -1;

	math_quat_from_plus_x_z(&x, &z, out);
}


static inline void
zldtt(const struct xrt_vec3 *trans, const struct xrt_quat *orientation, bool is_right, struct xrt_space_relation *out)
{

	out->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
	out->pose.position.x = trans->x;
	out->pose.position.y = trans->y;
	out->pose.position.z = trans->z;

	if (is_right) {
		zldtt_ori_right(orientation, &out->pose.orientation);
	} else {
		out->pose.orientation = *orientation;
	}
}


static void
our_eval_to_viz_hand(struct u_hand_sim_hand *opt,
                     struct translations55 *translations_absolute,
                     struct orientations54 *orientations_absolute,
                     bool is_right,
                     struct xrt_hand_joint_set *out_viz_hand)
{
	XRT_TRACE_MARKER();

	eval_hand_with_orientation(opt, is_right, translations_absolute, orientations_absolute);

	struct xrt_quat final_wrist_orientation = XRT_QUAT_IDENTITY;

	int joint_acc_idx = 0;

	// Palm.
	struct xrt_vec3 palm_position;
	palm_position.x = (translations_absolute->t[2][0].x + translations_absolute->t[2][1].x) / 2;
	palm_position.y = (translations_absolute->t[2][0].y + translations_absolute->t[2][1].y) / 2;
	palm_position.z = (translations_absolute->t[2][0].z + translations_absolute->t[2][1].z) / 2;

	struct xrt_quat *palm_orientation = &orientations_absolute->q[2][0];

	zldtt(&palm_position, palm_orientation, is_right,
	      &out_viz_hand->values.hand_joint_set_default[joint_acc_idx++].relation);

	// Wrist.
	zldtt(&opt->wrist_pose.pose.position, &final_wrist_orientation, is_right,
	      &out_viz_hand->values.hand_joint_set_default[joint_acc_idx++].relation);

	for (int finger = 0; finger < 5; finger++) {
		for (int joint = 0; joint < 5; joint++) {
			// This one is necessary
			if (finger == 0 && joint == 0) {
				continue;
			}
			struct xrt_quat *orientation;
			if (joint != 4) {
				orientation = &orientations_absolute->q[finger][joint];
			} else {
				orientation = &orientations_absolute->q[finger][joint - 1];
			}
			zldtt(&translations_absolute->t[finger][joint], orientation, is_right,
			      &out_viz_hand->values.hand_joint_set_default[joint_acc_idx++].relation);
		}
	}
	out_viz_hand->is_active = true;
}

static void
hand_sim_hand_init(struct u_hand_sim_hand *out_opt, enum xrt_hand xhand, const struct xrt_space_relation *root_pose)
{
	out_opt->hand_size = 0.095f;

	out_opt->is_right = xhand == XRT_HAND_RIGHT;
	out_opt->hand_pose = *root_pose;

	for (int i = 0; i < 4; i++) {
		//!@todo needed?
		out_opt->finger[i].metacarpal.swing.x = 0.f;
		out_opt->finger[i].metacarpal.twist = 0.f;

		out_opt->finger[i].proximal_swing.x = DEG_TO_RAD(15);
		out_opt->finger[i].rotations[0] = DEG_TO_RAD(-5);
		out_opt->finger[i].rotations[1] = DEG_TO_RAD(-5);
	}

	out_opt->thumb.metacarpal.swing.x = 0.f;
	out_opt->thumb.metacarpal.swing.y = 0.f;
	out_opt->thumb.metacarpal.twist = 0.f;

	out_opt->thumb.rotations[0] = DEG_TO_RAD(10);
	out_opt->thumb.rotations[1] = DEG_TO_RAD(10);

	out_opt->finger[0].metacarpal.swing.y = -0.19f;
	out_opt->finger[1].metacarpal.swing.y = 0.f;
	out_opt->finger[2].metacarpal.swing.y = 0.19f;
	out_opt->finger[3].metacarpal.swing.y = 0.38f;

	out_opt->finger[0].proximal_swing.y = -0.01f;
	out_opt->finger[1].proximal_swing.y = 0.f;
	out_opt->finger[2].proximal_swing.y = 0.01f;
	out_opt->finger[3].proximal_swing.y = 0.02f;
}


void
u_hand_sim_simulate(struct u_hand_sim_hand *hand_ptr, struct xrt_hand_joint_set *out_set)
{
	struct translations55 translations;
	struct orientations54 orientations;

	eval_hand_with_orientation(hand_ptr, hand_ptr->is_right, &translations, &orientations);

	our_eval_to_viz_hand(hand_ptr, &translations, &orientations, hand_ptr->is_right, out_set);

	u_hand_joints_apply_joint_width(out_set);

	out_set->hand_pose = hand_ptr->hand_pose;

	out_set->hand_pose.relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

	out_set->is_active = true;
}

void
u_hand_sim_simulate_for_valve_index_knuckles(const struct u_hand_tracking_curl_values *values,
                                             enum xrt_hand xhand,
                                             const struct xrt_space_relation *root_pose,
                                             struct xrt_hand_joint_set *out_set)
{
	struct u_hand_sim_hand hand;

	hand_sim_hand_init(&hand, xhand, root_pose);
	hand.wrist_pose.pose.position.x = 0.f;
	hand.wrist_pose.pose.position.y = 0.f;
	hand.wrist_pose.pose.position.z = 0.f;

	hand.hand_size = 0.095;

	// Thumb
	hand.thumb.metacarpal.swing.x += values->thumb * 0.08f;
	hand.thumb.metacarpal.swing.y += -0.35f;
	hand.thumb.metacarpal.twist = 0;
	hand.thumb.rotations[0] += values->thumb * -1.f;
	hand.thumb.rotations[1] += values->thumb * -1.f;

	// Index finger - this is treated differently on Valve Knuckles controllers so the pinch gesture feels good
	float finger_values[4] = {values->index, values->middle, values->ring, values->little};

	{
		int finger = 0;
		float val_turn = finger_values[finger] * -1.1f;
		hand.finger[finger].proximal_swing.x = val_turn * 0.9f;
		hand.finger[finger].rotations[0] = val_turn;
		hand.finger[finger].rotations[1] = val_turn;
	}

	for (int finger = 1; finger < 4; finger++) {
		float val_turn = finger_values[finger] * -1.1f * 1.3f;
		hand.finger[finger].proximal_swing.x = val_turn * 1.3f;
		hand.finger[finger].rotations[0] = val_turn * 1.0f;
		hand.finger[finger].rotations[1] = val_turn * 0.4f;
	}

	u_hand_sim_simulate(&hand, out_set);
}

static void
u_hand_sim_apply_generic_finger_transform(const struct u_hand_tracking_finger_value *finger_value,
                                          struct u_hand_sim_finger *out_finger)
{
	out_finger->metacarpal.swing.x = finger_value->joint_curls[0] * -1.f;

	out_finger->proximal_swing.x = finger_value->joint_curls[1] * -1.f;
	out_finger->proximal_swing.y = finger_value->splay;

	out_finger->rotations[0] = finger_value->joint_curls[2] * -1.f;
	out_finger->rotations[1] = finger_value->joint_curls[3] * -1.f;
}

void
u_hand_sim_simulate_generic(const struct u_hand_tracking_values *values,
                            enum xrt_hand xhand,
                            const struct xrt_space_relation *root_pose,
                            struct xrt_hand_joint_set *out_set)
{
	struct u_hand_sim_hand hand;

	hand_sim_hand_init(&hand, xhand, root_pose);
	hand.wrist_pose.pose.position.x = 0.f;
	hand.wrist_pose.pose.position.y = 0.f;
	hand.wrist_pose.pose.position.z = 0.f;

	hand.hand_size = 0.095;

	// Thumb
	hand.thumb.metacarpal.swing.x += values->thumb.joint_curls[0] * 0.08f; // curl

	hand.thumb.metacarpal.swing.y += values->thumb.splay; // splay
	hand.thumb.metacarpal.twist = 0;
	hand.thumb.rotations[0] += values->thumb.joint_curls[1] * -1.f;
	hand.thumb.rotations[1] += values->thumb.joint_curls[2] * -1.f;

	u_hand_sim_apply_generic_finger_transform(&values->little, &hand.finger[3]);
	u_hand_sim_apply_generic_finger_transform(&values->ring, &hand.finger[2]);
	u_hand_sim_apply_generic_finger_transform(&values->middle, &hand.finger[1]);
	u_hand_sim_apply_generic_finger_transform(&values->index, &hand.finger[0]);

	u_hand_sim_simulate(&hand, out_set);
}
