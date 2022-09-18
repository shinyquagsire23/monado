// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Levenberg-Marquardt kinematic optimizer
 * @author Moses Turner <moses@collabora.com>
 * @author Charlton Rodda <charlton.rodda@collabora.com>
 * @ingroup tracking
 */

#include "math/m_api.h"
#include "math/m_vec3.h"
#include "os/os_time.h"
#include "util/u_logging.h"
#include "util/u_misc.h"
#include "util/u_trace_marker.h"

#include "tinyceres/tiny_solver.hpp"
#include "tinyceres/tiny_solver_autodiff_function.hpp"
#include "lm_rotations.inl"

#include <iostream>
#include <cmath>
#include <random>
#include "lm_interface.hpp"
#include "lm_optimizer_params_packer.inl"
#include "lm_defines.hpp"

/*

Some notes:
Everything templated with <typename T> is basically just a scalar template, usually taking float or ceres::Jet<float, N>

*/

namespace xrt::tracking::hand::mercury::lm {

template <typename T> struct StereographicObservation
{
	// T obs[kNumNNJoints][2];
	Vec2<T> obs[kNumNNJoints];
};

struct KinematicHandLM
{
	bool first_frame = true;
	bool use_stability = false;
	bool optimize_hand_size = true;
	bool is_right = false;
	int num_observation_views = 0;
	one_frame_input *observation;

	HandScalar target_hand_size;
	HandScalar hand_size_err_mul;
	u_logging_level log_level;


	StereographicObservation<HandScalar> sgo[2];

	Quat<HandScalar> last_frame_pre_rotation;
	OptimizerHand<HandScalar> last_frame;

	// The pose that will take you from the right camera's space to the left camera's space.
	xrt_pose left_in_right;

	// The translation part of the same pose, just easier for Ceres to consume
	Vec3<HandScalar> left_in_right_translation;

	// The orientation part of the same pose, just easier for Ceres to consume
	Quat<HandScalar> left_in_right_orientation;

	Eigen::Matrix<HandScalar, calc_input_size(true), 1> TinyOptimizerInput;
};

template <typename T> struct Translations55
{
	Vec3<T> t[kNumFingers][kNumJointsInFinger];
};

template <typename T> struct Orientations54
{
	Quat<T> q[kNumFingers][kNumJointsInFinger];
};

template <bool optimize_hand_size> struct CostFunctor
{
	KinematicHandLM &parent;
	size_t num_residuals_;

	template <typename T>
	bool
	operator()(const T *const x, T *residual) const;

	CostFunctor(KinematicHandLM &in_last_hand, size_t const &num_residuals)
	    : parent(in_last_hand), num_residuals_(num_residuals)
	{}

	size_t
	NumResiduals() const
	{
		return num_residuals_;
	}
};

template <typename T>
static inline void
eval_hand_set_rel_translations(const OptimizerHand<T> &opt, Translations55<T> &rel_translations)
{
	// Basically, we're walking up rel_translations, writing strictly sequentially. Hopefully this is fast.

	// Thumb metacarpal translation.
	rel_translations.t[0][0] = {(T)0.33097, T(-0.1), (T)-0.25968};

	// Comes after the invisible joint.
	rel_translations.t[0][1] = {T(0), T(0), T(0)};
	// prox, distal, tip
	rel_translations.t[0][2] = {T(0), T(0), T(-0.389626)};
	rel_translations.t[0][3] = {T(0), T(0), T(-0.311176)};
	rel_translations.t[0][4] = {T(0), T(0), (T)-0.232195};

	// What's the best place to put this? Here works, but is there somewhere we could put it where it gets accessed
	// faster?
	T finger_joint_lengths[4][4] = {
	    {
	        T(-0.66),
	        T(-0.365719),
	        T(-0.231581),
	        T(-0.201790),
	    },
	    {
	        T(-0.645),
	        T(-0.404486),
	        T(-0.247749),
	        T(-0.210121),
	    },
	    {
	        T(-0.58),
	        T(-0.365639),
	        T(-0.225666),
	        T(-0.187089),
	    },
	    {
	        T(-0.52),
	        T(-0.278197),
	        T(-0.176178),
	        T(-0.157566),
	    },
	};

	// Index metacarpal
	rel_translations.t[1][0] = {T(0.16926), T(0), T(-0.34437)};
	// Middle
	rel_translations.t[2][0] = {T(0.034639), T(0.01), T(-0.35573)};
	// Ring
	rel_translations.t[3][0] = {T(-0.063625), T(0.005), T(-0.34164)};
	// Little
	rel_translations.t[4][0] = {T(-0.1509), T(-0.005), T(-0.30373)};

	// Index to little finger
	for (int finger = 0; finger < 4; finger++) {
		for (int i = 0; i < 4; i++) {
			int bone = i + 1;
			rel_translations.t[finger + 1][bone] = {T(0), T(0), T(finger_joint_lengths[finger][i])};
		}
	}

	// This is done in UnitQuaternionRotateAndScale now.
	// for (int finger = 0; finger < 5; finger++) {
	// 	for (int bone = 0; bone < 5; bone++) {
	// 		rel_translations[finger][bone][0] *= opt.hand_size;
	// 		rel_translations[finger][bone][1] *= opt.hand_size;
	// 		rel_translations[finger][bone][2] *= opt.hand_size;
	// 	}
	// }
}



template <typename T>
inline void
eval_hand_set_rel_orientations(const OptimizerHand<T> &opt, Orientations54<T> &rel_orientations)
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
	rel_orientations.q[0][0].w = T(0.716990172863006591796875);
	rel_orientations.q[0][0].x = T(0.1541481912136077880859375);
	rel_orientations.q[0][0].y = T(-0.31655871868133544921875);
	rel_orientations.q[0][0].z = T(-0.6016261577606201171875);
#endif

	// Thumb MCP orientation
	SwingTwistToQuaternion(opt.thumb.metacarpal.swing, //
	                       opt.thumb.metacarpal.twist, //
	                       rel_orientations.q[0][1]);

	// Thumb curls
	CurlToQuaternion(opt.thumb.rots[0], rel_orientations.q[0][2]);
	CurlToQuaternion(opt.thumb.rots[1], rel_orientations.q[0][3]);

	// Finger orientations
	for (int i = 0; i < 4; i++) {
		SwingTwistToQuaternion(opt.finger[i].metacarpal.swing, //
		                       opt.finger[i].metacarpal.twist, //
		                       rel_orientations.q[i + 1][0]);

		SwingToQuaternion(opt.finger[i].proximal_swing, //
		                  rel_orientations.q[i + 1][1]);

		CurlToQuaternion(opt.finger[i].rots[0], rel_orientations.q[i + 1][2]);
		CurlToQuaternion(opt.finger[i].rots[1], rel_orientations.q[i + 1][3]);
	}
}



template <typename T>
void
eval_hand_with_orientation(const OptimizerHand<T> &opt,
                           bool is_right,
                           Translations55<T> &translations_absolute,
                           Orientations54<T> &orientations_absolute)

{
	XRT_TRACE_MARKER();


	Translations55<T> rel_translations; //[kNumFingers][kNumJointsInFinger];
	Orientations54<T> rel_orientations; //[kNumFingers][kNumOrientationsInFinger];

	eval_hand_set_rel_orientations(opt, rel_orientations);

	eval_hand_set_rel_translations(opt, rel_translations);

	Quat<T> orientation_root;

	Quat<T> post_orientation_quat;

	AngleAxisToQuaternion(opt.wrist_post_orientation_aax, post_orientation_quat);

	QuaternionProduct(opt.wrist_pre_orientation_quat, post_orientation_quat, orientation_root);

	// Get each joint's tracking-relative orientation by rotating its parent-relative orientation by the
	// tracking-relative orientation of its parent.
	for (size_t finger = 0; finger < kNumFingers; finger++) {
		Quat<T> *last_orientation = &orientation_root;
		for (size_t bone = 0; bone < kNumOrientationsInFinger; bone++) {
			Quat<T> &out_orientation = orientations_absolute.q[finger][bone];
			Quat<T> &rel_orientation = rel_orientations.q[finger][bone];

			QuaternionProduct(*last_orientation, rel_orientation, out_orientation);
			last_orientation = &out_orientation;
		}
	}

	// Get each joint's tracking-relative position by rotating its parent-relative translation by the
	// tracking-relative orientation of its parent, then adding that to its parent's tracking-relative position.
	for (size_t finger = 0; finger < kNumFingers; finger++) {
		const Vec3<T> *last_translation = &opt.wrist_location;
		const Quat<T> *last_orientation = &orientation_root;
		for (size_t bone = 0; bone < kNumJointsInFinger; bone++) {
			Vec3<T> &out_translation = translations_absolute.t[finger][bone];
			Vec3<T> &rel_translation = rel_translations.t[finger][bone];

			UnitQuaternionRotateAndScalePoint(*last_orientation, rel_translation, opt.hand_size,
			                                  out_translation);

			// If this is a right hand, mirror it.
			if (is_right) {
				out_translation.x *= -1;
			}

			out_translation.x += last_translation->x;
			out_translation.y += last_translation->y;
			out_translation.z += last_translation->z;

			// Next iteration, the orientation to rotate by should be the tracking-relative orientation of
			// this joint.

			// If bone < 4 so we don't go over the end of orientations_absolute. I hope this gets optimized
			// out anyway.
			if (bone < 4) {
				last_orientation = &orientations_absolute.q[finger][bone];
				// Ditto for translation
				last_translation = &out_translation;
			}
		}
	}
}

template <typename T>
void
computeResidualStability_Finger(const OptimizerFinger<T> &finger,
                                const OptimizerFinger<HandScalar> &finger_last,
                                ResidualHelper<T> &helper)
{
	helper.AddValue((finger.metacarpal.swing.x - finger_last.metacarpal.swing.x) * kStabilityFingerMCPSwing);

	helper.AddValue((finger.metacarpal.swing.y - finger_last.metacarpal.swing.y) * kStabilityFingerMCPSwing);



	helper.AddValue((finger.metacarpal.twist - finger_last.metacarpal.twist) * kStabilityFingerMCPTwist);



	helper.AddValue((finger.proximal_swing.x - finger_last.proximal_swing.x) * kStabilityFingerPXMSwingX);
	helper.AddValue((finger.proximal_swing.y - finger_last.proximal_swing.y) * kStabilityFingerPXMSwingY);

	helper.AddValue((finger.rots[0] - finger_last.rots[0]) * kStabilityCurlRoot);
	helper.AddValue((finger.rots[1] - finger_last.rots[1]) * kStabilityCurlRoot);

#ifdef USE_HAND_PLAUSIBILITY
	if (finger.rots[0] < finger.rots[1]) {
		helper.AddValue((finger.rots[0] - finger.rots[1]) * kPlausibilityCurlSimilarityHard);
	} else {
		helper.AddValue((finger.rots[0] - finger.rots[1]) * kPlausibilityCurlSimilaritySoft);
	}
#endif
}

template <bool optimize_hand_size, typename T>
void
computeResidualStability(const OptimizerHand<T> &hand,
                         const OptimizerHand<HandScalar> &last_hand,
                         KinematicHandLM &state,
                         ResidualHelper<T> &helper)
{

	if constexpr (optimize_hand_size) {
		helper.AddValue( //
		    (hand.hand_size - state.target_hand_size) * (T)(kStabilityHandSize * state.hand_size_err_mul));
	}
	if (state.first_frame) {
		return;
	}

	helper.AddValue((last_hand.wrist_location.x - hand.wrist_location.x) * kStabilityRootPosition);
	helper.AddValue((last_hand.wrist_location.y - hand.wrist_location.y) * kStabilityRootPosition);
	helper.AddValue((last_hand.wrist_location.z - hand.wrist_location.z) * kStabilityRootPosition);


	helper.AddValue((hand.wrist_post_orientation_aax.x) * (T)(kStabilityHandOrientation));
	helper.AddValue((hand.wrist_post_orientation_aax.y) * (T)(kStabilityHandOrientation));
	helper.AddValue((hand.wrist_post_orientation_aax.z) * (T)(kStabilityHandOrientation));



	helper.AddValue((hand.thumb.metacarpal.swing.x - last_hand.thumb.metacarpal.swing.x) * kStabilityThumbMCPSwing);
	helper.AddValue((hand.thumb.metacarpal.swing.y - last_hand.thumb.metacarpal.swing.y) * kStabilityThumbMCPSwing);
	helper.AddValue((hand.thumb.metacarpal.twist - last_hand.thumb.metacarpal.twist) * kStabilityThumbMCPTwist);

	helper.AddValue((hand.thumb.rots[0] - last_hand.thumb.rots[0]) * kStabilityCurlRoot);
	helper.AddValue((hand.thumb.rots[1] - last_hand.thumb.rots[1]) * kStabilityCurlRoot);
#ifdef USE_HAND_PLAUSIBILITY
	helper.AddValue((hand.finger[1].proximal_swing.x - hand.finger[2].proximal_swing.x) *
	                kPlausibilityProximalSimilarity);
	helper.AddValue((hand.finger[2].proximal_swing.x - hand.finger[3].proximal_swing.x) *
	                kPlausibilityProximalSimilarity);
#endif


	for (int finger_idx = 0; finger_idx < 4; finger_idx++) {
		const OptimizerFinger<HandScalar> &finger_last = last_hand.finger[finger_idx];

		const OptimizerFinger<T> &finger = hand.finger[finger_idx];

		computeResidualStability_Finger(finger, finger_last, helper);
	}
}

template <typename T>
inline void
normalize_vector_inplace(Vec3<T> &vector)
{
	T len = (T)(0);

	len += vector.x * vector.x;
	len += vector.y * vector.y;
	len += vector.z * vector.z;

	len = sqrt(len);

	// This is *a* solution ;)
	//!@todo any good template ways to get epsilon for float,double,jet?
	if (len <= FLT_EPSILON) {
		vector.z = T(-1);
		return;
	}

	vector.x /= len;
	vector.y /= len;
	vector.z /= len;
}

// in size: 3, out size: 2
template <typename T>
static inline void
unit_vector_stereographic_projection(const Vec3<T> &in, Vec2<T> &out)
{
	out.x = in.x / ((T)1 - in.z);
	out.y = in.y / ((T)1 - in.z);
}


template <typename T>
static inline void
unit_xrt_vec3_stereographic_projection(const xrt_vec3 in, Vec2<T> &out)
{
	Vec3<T> vec;
	vec.x = (T)(in.x);
	vec.y = (T)(in.y);
	vec.z = (T)(in.z);

	normalize_vector_inplace(vec);

	unit_vector_stereographic_projection(vec, out);
}

template <typename T>
static void
diff(const Vec3<T> &model_joint_pos,
     const Vec3<T> &move_joint_translation,
     const Quat<T> &move_joint_orientation,
     const StereographicObservation<HandScalar> &observation,
     const HandScalar *confidences,
     const HandScalar amount_we_care,
     int &hand_joint_idx,
     ResidualHelper<T> &helper)
{

	Vec3<T> model_joint_dir_rel_camera;
	UnitQuaternionRotatePoint(move_joint_orientation, model_joint_pos, model_joint_dir_rel_camera);

	model_joint_dir_rel_camera.x = model_joint_dir_rel_camera.x + move_joint_translation.x;
	model_joint_dir_rel_camera.y = model_joint_dir_rel_camera.y + move_joint_translation.y;
	model_joint_dir_rel_camera.z = model_joint_dir_rel_camera.z + move_joint_translation.z;

	normalize_vector_inplace(model_joint_dir_rel_camera);

	Vec2<T> stereographic_model_dir;
	unit_vector_stereographic_projection(model_joint_dir_rel_camera, stereographic_model_dir);


	const HandScalar confidence = confidences[hand_joint_idx] * amount_we_care;
	const Vec2<T> &observed_ray_sg = observation.obs[hand_joint_idx];

	helper.AddValue((stereographic_model_dir.x - (T)(observed_ray_sg.x)) * confidence);
	helper.AddValue((stereographic_model_dir.y - (T)(observed_ray_sg.y)) * confidence);

	hand_joint_idx++;
}



template <typename T>
void
CostFunctor_PositionsPart(OptimizerHand<T> &hand, KinematicHandLM &state, ResidualHelper<T> &helper)
{

	Translations55<T> translations_absolute;
	Orientations54<T> orientations_absolute;

	HandScalar we_care_joint[] = {1.3, 0.9, 0.9, 1.3};
	HandScalar we_care_finger[] = {1.0, 1.0, 0.8, 0.8, 0.8};

	eval_hand_with_orientation(hand, state.is_right, translations_absolute, orientations_absolute);

	for (int view = 0; view < 2; view++) {
		if (!state.observation->views[view].active) {
			continue;
		}
		Vec3<T> move_direction;
		Quat<T> move_orientation;

		if (view == 0) {
			move_direction = Vec3<T>::Zero();
			move_orientation = Quat<T>::Identity();
		} else {
			move_direction.x = T(state.left_in_right_translation.x);
			move_direction.y = T(state.left_in_right_translation.y);
			move_direction.z = T(state.left_in_right_translation.z);

			move_orientation.w = T(state.left_in_right_orientation.w);
			move_orientation.x = T(state.left_in_right_orientation.x);
			move_orientation.y = T(state.left_in_right_orientation.y);
			move_orientation.z = T(state.left_in_right_orientation.z);
		}
		int joint_acc_idx = 0;

		HandScalar *confidences = state.observation->views[view].confidences;

		diff<T>(hand.wrist_location, move_direction, move_orientation, state.sgo[view], confidences, 1.5,
		        joint_acc_idx, helper);

		for (int finger_idx = 0; finger_idx < 5; finger_idx++) {
			for (int joint_idx = 0; joint_idx < 4; joint_idx++) {
				diff<T>(translations_absolute.t[finger_idx][joint_idx + 1], move_direction,
				        move_orientation, state.sgo[view], confidences,
				        we_care_finger[finger_idx] * we_care_joint[joint_idx], joint_acc_idx, helper);
			}
		}
	}
}

// template <typename T>
// void CostFunctor_HandSizePart(OptimizerHand<T> &hand, KinematicHandLM &state, T *residual, size_t &out_residual_idx)
// {

// }

template <bool optimize_hand_size>
template <typename T>
bool
CostFunctor<optimize_hand_size>::operator()(const T *const x, T *residual) const
{
	struct KinematicHandLM &state = this->parent;
	OptimizerHand<T> hand = {};
	// ??? should I do the below? probably.
	Quat<T> tmp = this->parent.last_frame_pre_rotation;
	OptimizerHandInit<T>(hand, tmp);
	OptimizerHandUnpackFromVector(x, state.optimize_hand_size, T(state.target_hand_size), hand);

	XRT_MAYBE_UNUSED size_t residual_size =
	    calc_residual_size(state.use_stability, optimize_hand_size, state.num_observation_views);

// When you're hacking, you want to set the residuals to always-0 so that any of them you forget to touch keep their 0
// gradient.
// But then later this just becomes a waste.
#if 0	
	for (size_t i = 0; i < residual_size; i++) {
		residual[i] = (T)(0);
	}
#endif

	ResidualHelper<T> helper(residual);

	CostFunctor_PositionsPart(hand, state, helper);
	computeResidualStability<optimize_hand_size, T>(hand, state.last_frame, state, helper);

	// Bounds checking - we should have written exactly to the end.
	// U_LOG_E("%zu %zu", helper.out_residual_idx, residual_size);
	assert(helper.out_residual_idx == residual_size);
	// If you're hacking, feel free to turn this off; just remember to not write off the end, and to initialize
	// everything somewhere (maybe change the above to an #if 1? )

	return true;
}

// look at tests_quat_change_of_basis
#if 0
template <typename T>
static inline void
zldtt_ori_right(Quat<T> &orientation, xrt_quat *out)
{
	struct xrt_quat tmp;
	tmp.w = orientation.w;
	tmp.x = orientation.x;
	tmp.y = orientation.y;
	tmp.z = orientation.z;

	xrt_vec3 x = XRT_VEC3_UNIT_X;
	xrt_vec3 z = XRT_VEC3_UNIT_Z;

	math_quat_rotate_vec3(&tmp, &x, &x);
	math_quat_rotate_vec3(&tmp, &z, &z);

	// This is a very squashed change-of-basis from left-handed coordinate systems to right-handed coordinate
	// systems: you multiply everything by (-1 0 0) then negate the X axis.

	x.y *= -1;
	x.z *= -1;

	z.x *= -1;

	math_quat_from_plus_x_z(&x, &z, out);
}
#else
template <typename T>
static inline void
zldtt_ori_right(Quat<T> &orientation, xrt_quat *out)
{
	out->x = -orientation.x;
	out->y = orientation.y;
	out->z = orientation.z;
	out->w = -orientation.w;
}
#endif

template <typename T>
static inline void
zldtt_ori_left(Quat<T> &orientation, xrt_quat *out)
{
	out->w = orientation.w;
	out->x = orientation.x;
	out->y = orientation.y;
	out->z = orientation.z;
}

template <typename T>
static inline void
zldtt(Vec3<T> &trans, Quat<T> &orientation, bool is_right, xrt_space_relation &out)
{

	out.relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
	out.pose.position.x = trans.x;
	out.pose.position.y = trans.y;
	out.pose.position.z = trans.z;
	if (is_right) {
		zldtt_ori_right(orientation, &out.pose.orientation);
	} else {
		zldtt_ori_left(orientation, &out.pose.orientation);
	}
}

static void
eval_to_viz_hand(KinematicHandLM &state, xrt_hand_joint_set &out_viz_hand)
{
	XRT_TRACE_MARKER();

	//!@todo It's _probably_ fine to have the bigger size?
	Eigen::Matrix<HandScalar, calc_input_size(true), 1> pose = state.TinyOptimizerInput.cast<HandScalar>();

	OptimizerHand<HandScalar> opt = {};
	OptimizerHandInit(opt, state.last_frame_pre_rotation);
	OptimizerHandUnpackFromVector(pose.data(), state.optimize_hand_size, state.target_hand_size, opt);

	Translations55<HandScalar> translations_absolute;
	Orientations54<HandScalar> orientations_absolute;
	// Vec3<HandScalar> translations_absolute[kNumFingers][kNumJointsInFinger];
	// Quat<HandScalar> orientations_absolute[kNumFingers][kNumOrientationsInFinger];

	eval_hand_with_orientation(opt, state.is_right, translations_absolute, orientations_absolute);

	Quat<HandScalar> post_wrist_orientation;

	AngleAxisToQuaternion(opt.wrist_post_orientation_aax, post_wrist_orientation);

	Quat<HandScalar> pre_wrist_orientation = state.last_frame_pre_rotation;

	// for (int i = 0; i < 4; i++) {
	// 	pre_wrist_orientation[i] = state.last_frame_pre_rotation[i];
	// }

	Quat<HandScalar> final_wrist_orientation;

	QuaternionProduct(pre_wrist_orientation, post_wrist_orientation, final_wrist_orientation);

	int joint_acc_idx = 0;

	// Palm.

	Vec3<HandScalar> palm_position;
	palm_position.x = (translations_absolute.t[2][0].x + translations_absolute.t[2][1].x) / 2;
	palm_position.y = (translations_absolute.t[2][0].y + translations_absolute.t[2][1].y) / 2;
	palm_position.z = (translations_absolute.t[2][0].z + translations_absolute.t[2][1].z) / 2;

	Quat<HandScalar> &palm_orientation = orientations_absolute.q[2][0];

	zldtt(palm_position, palm_orientation, state.is_right,
	      out_viz_hand.values.hand_joint_set_default[joint_acc_idx++].relation);

	// Wrist.
	zldtt(opt.wrist_location, final_wrist_orientation, state.is_right,
	      out_viz_hand.values.hand_joint_set_default[joint_acc_idx++].relation);

	for (int finger = 0; finger < 5; finger++) {
		for (int joint = 0; joint < 5; joint++) {
			// This one is necessary
			if (finger == 0 && joint == 0) {
				continue;
			}
			Quat<HandScalar> *orientation;
			if (joint != 4) {
				orientation = &orientations_absolute.q[finger][joint];
			} else {
				orientation = &orientations_absolute.q[finger][joint - 1];
			}
			zldtt(translations_absolute.t[finger][joint], *orientation, state.is_right,
			      out_viz_hand.values.hand_joint_set_default[joint_acc_idx++].relation);
		}
	}
	out_viz_hand.is_active = true;
}

//!@todo
// This reprojection error thing is probably the wrong way of doing it.
// I think that the right way is to hack (?) TinySolver such that we get the last set of residuals back, and use those
// somehow. Maybe scale them simply by the spread of the input hand rays? What about handling stereographic projections?
// worth it? The main bit is that we should just use the last set of residuals somehow, calculating all this is a waste
// of cycles when we have something that already should work.

static void
repro_error_make_joint_ray(const xrt_hand_joint_value &joint, const xrt_pose &cam_pose, xrt_vec3 &out_ray)
{
	// by VALUE
	xrt_vec3 position = joint.relation.pose.position;

	math_quat_rotate_vec3(&cam_pose.orientation, &position, &position);
	position = position + cam_pose.position;

	out_ray = m_vec3_normalize(position);
}

static enum xrt_hand_joint
joint_from_21(int finger, int joint)
{
	if (finger > 0) {
		return xrt_hand_joint(2 + (finger * 5) + joint);
	} else {
		return xrt_hand_joint(joint + 3);
	}
}

static inline float
simple_vec3_difference(const xrt_vec3 one, const xrt_vec3 two)
{
	return (1.0 - m_vec3_dot(one, two));
}

float
reprojection_error_2d(KinematicHandLM &state, const one_frame_input &observation, const xrt_hand_joint_set &set)
{
	float final_err = 0;
	int views_looked_at = 0;
	for (int view = 0; view < 2; view++) {
		if (!observation.views[view].active) {
			continue;
		}
		views_looked_at++;
		xrt_pose move_amount;

		if (view == 0) {
			// left camera.
			move_amount = XRT_POSE_IDENTITY;
		} else {
			move_amount = state.left_in_right;
		}

		xrt_vec3 lm_rays[21];
		const xrt_vec3 *obs_rays = observation.views[view].rays;


		int acc_idx = 0;

		repro_error_make_joint_ray(set.values.hand_joint_set_default[XRT_HAND_JOINT_WRIST], move_amount,
		                           lm_rays[acc_idx++]);

		for (int finger = 0; finger < 5; finger++) {
			for (int joint = 0; joint < 4; joint++) {
				repro_error_make_joint_ray(
				    set.values.hand_joint_set_default[joint_from_21(finger, joint)], move_amount,
				    lm_rays[acc_idx++]);
			}
		}

		xrt_vec3 obs_center = {};
		xrt_vec3 lm_center = {};

		float err = 0;
		float obs_spread = 0;
		float lm_spread = 0;

		for (int i = 0; i < 21; i++) {
			obs_center += obs_rays[i];
			lm_center += lm_rays[i];
			err += simple_vec3_difference(lm_rays[i], obs_rays[i]);
		}

		math_vec3_scalar_mul(1.0f / 21.0f, &obs_center);
		math_vec3_scalar_mul(1.0f / 21.0f, &lm_center);

		for (int i = 0; i < 21; i++) {
			obs_spread += simple_vec3_difference(obs_center, obs_rays[i]);
		}
		for (int i = 0; i < 21; i++) {
			lm_spread += simple_vec3_difference(lm_center, lm_rays[i]);
		}



		// std::cout << err << std::endl;
		// std::cout << err / (obs_spread + lm_spread) << std::endl;

		// return ;
		final_err += err / (obs_spread + lm_spread);
	}
	return final_err / (float)views_looked_at;
}

template <bool optimize_hand_size>
inline float
opt_run(KinematicHandLM &state, one_frame_input &observation, xrt_hand_joint_set &out_viz_hand)
{
	constexpr size_t input_size = calc_input_size(optimize_hand_size);

	size_t residual_size = calc_residual_size(state.use_stability, optimize_hand_size, state.num_observation_views);

	LM_DEBUG(state, "Running with %zu inputs and %zu residuals, viewed in %d cameras", input_size, residual_size,
	         state.num_observation_views);

	CostFunctor<optimize_hand_size> cf(state, residual_size);

	using AutoDiffCostFunctor =
	    ceres::TinySolverAutoDiffFunction<CostFunctor<optimize_hand_size>, Eigen::Dynamic, input_size, HandScalar>;

	AutoDiffCostFunctor f(cf);


	// Okay I have no idea if this should be {}-initialized or not. Previous me seems to have thought no, but it
	// works either way.
	ceres::TinySolver<AutoDiffCostFunctor> solver = {};
	solver.options.max_num_iterations = 50;
	// We need to do a parameter sweep for the trust region and see what's fastest.
	// solver.options.initial_trust_region_radius = 1e3;
	solver.options.function_tolerance = 1e-6;

	Eigen::Matrix<HandScalar, input_size, 1> inp = state.TinyOptimizerInput.head<input_size>();

	XRT_MAYBE_UNUSED uint64_t start = os_monotonic_get_ns();
	XRT_MAYBE_UNUSED auto summary = solver.Solve(f, &inp);
	XRT_MAYBE_UNUSED uint64_t end = os_monotonic_get_ns();

	//!@todo Is there a zero-copy way of doing this?
	state.TinyOptimizerInput.head<input_size>() = inp;

	if (state.log_level <= U_LOGGING_DEBUG) {

		uint64_t diff = end - start;
		double time_taken = (double)diff / (double)U_TIME_1MS_IN_NS;

		const char *status;

		switch (summary.status) {
		case 0: {
			status = "GRADIENT_TOO_SMALL";
		} break;
		case 1: {
			status = "RELATIVE_STEP_SIZE_TOO_SMALL";
		} break;
		case 2: {
			status = "COST_TOO_SMALL";
		} break;
		case 3: {
			status = "HIT_MAX_ITERATIONS";
		} break;
		case 4: {
			status = "COST_CHANGE_TOO_SMALL";
		} break;
		}

		LM_DEBUG(state, "Status: %s, num_iterations %d, max_norm %E, gtol %E", status, summary.iterations,
		         summary.gradient_max_norm, solver.options.gradient_tolerance);
		LM_DEBUG(state, "Took %f ms", time_taken);
		if (summary.iterations < 3) {
			LM_DEBUG(state, "Suspiciouisly low number of iterations!");
		}
	}
	return 0;
}


void
hand_was_untracked(KinematicHandLM *hand)
{
	hand->first_frame = true;
	hand->last_frame_pre_rotation.w = 1.0;
	hand->last_frame_pre_rotation.x = 0.0;
	hand->last_frame_pre_rotation.y = 0.0;
	hand->last_frame_pre_rotation.z = 0.0;

	OptimizerHandInit(hand->last_frame, hand->last_frame_pre_rotation);
	OptimizerHandPackIntoVector(hand->last_frame, hand->optimize_hand_size, hand->TinyOptimizerInput.data());
}

void
optimizer_run(KinematicHandLM *hand,
              one_frame_input &observation,
              bool hand_was_untracked_last_frame,
              bool optimize_hand_size,
              float target_hand_size,
              float hand_size_err_mul,
              xrt_hand_joint_set &out_viz_hand,
              float &out_hand_size,
              float &out_reprojection_error) // NOLINT(bugprone-easily-swappable-parameters)
{
	KinematicHandLM &state = *hand;

	if (hand_was_untracked_last_frame) {
		hand_was_untracked(hand);
	}

	state.num_observation_views = 0;
	for (int i = 0; i < 2; i++) {
		if (observation.views[i].active) {
			state.num_observation_views++;
		}
	}

	state.optimize_hand_size = optimize_hand_size;
	state.target_hand_size = target_hand_size;
	state.hand_size_err_mul = hand_size_err_mul;

	state.use_stability = !state.first_frame;

	state.observation = &observation;

	for (int i = 0; i < 21; i++) {
		for (int view = 0; view < 2; view++) {
			unit_xrt_vec3_stereographic_projection(observation.views[view].rays[i], state.sgo[view].obs[i]);
		}
	}


	// For now, we have to statically instantiate different versions of the optimizer depending on how many input
	// parameters there are. For now, there are only two cases - either we are optimizing the hand size or we are
	// not optimizing it.
	// !@todo Can we make a magic template that automatically instantiates the right one, and also make it so we can
	// decide to either make the residual size dynamic or static? Currently, it's dynamic, which is easier for us
	// and makes compile times a lot lower, but it probably makes things some amount slower at runtime.

	if (optimize_hand_size) {
		opt_run<true>(state, observation, out_viz_hand);
	} else {
		opt_run<false>(state, observation, out_viz_hand);
	}



	// Postfix - unpack,
	OptimizerHandUnpackFromVector(state.TinyOptimizerInput.data(), state.optimize_hand_size, state.target_hand_size,
	                              state.last_frame);



	// Squash the orientations
	OptimizerHandSquashRotations(state.last_frame, state.last_frame_pre_rotation);

	// Repack - brings the curl values back into original domain. Look at ModelToLM/LMToModel, we're using sin/asin.
	OptimizerHandPackIntoVector(state.last_frame, hand->optimize_hand_size, state.TinyOptimizerInput.data());



	eval_to_viz_hand(state, out_viz_hand);

	state.first_frame = false;

	out_hand_size = state.last_frame.hand_size;

	out_reprojection_error = reprojection_error_2d(state, observation, out_viz_hand);
}



void
optimizer_create(xrt_pose left_in_right, bool is_right, u_logging_level log_level, KinematicHandLM **out_kinematic_hand)
{
	KinematicHandLM *hand = new KinematicHandLM;

	hand->is_right = is_right;
	hand->left_in_right = left_in_right;
	hand->log_level = log_level;

	hand->left_in_right_translation.x = left_in_right.position.x;
	hand->left_in_right_translation.y = left_in_right.position.y;
	hand->left_in_right_translation.z = left_in_right.position.z;

	hand->left_in_right_orientation.w = left_in_right.orientation.w;
	hand->left_in_right_orientation.x = left_in_right.orientation.x;
	hand->left_in_right_orientation.y = left_in_right.orientation.y;
	hand->left_in_right_orientation.z = left_in_right.orientation.z;

	// Probably unnecessary.
	hand_was_untracked(hand);

	*out_kinematic_hand = hand;
}

void
optimizer_destroy(KinematicHandLM **hand)
{
	delete *hand;
	hand = NULL;
}
} // namespace xrt::tracking::hand::mercury::lm
