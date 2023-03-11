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

#include "../hg_numerics_checker.hpp"
#include "lm_hand_init_guesser.hpp"

/*

Some notes:
Everything templated with <typename T> is basically just a scalar template, usually taking float or ceres::Jet<float, N>

*/

namespace xrt::tracking::hand::mercury::lm {

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
		//!@todo: In this version of our tracking, this is always constant.
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
eval_hand_with_orientation(const KinematicHandLM &state,
                           const OptimizerHand<T> &opt,
                           const bool is_right,
                           Translations55<T> &translations_absolute,
                           Orientations54<T> &orientations_absolute)

{
	XRT_TRACE_MARKER();


	Translations55<T> rel_translations = {};
	Orientations54<T> rel_orientations = {};

	eval_hand_set_rel_orientations(opt, rel_orientations);

	eval_hand_set_rel_translations(opt, rel_translations);


	// Get each joint's tracking-relative orientation by rotating its parent-relative orientation by the
	// tracking-relative orientation of its parent.
	for (size_t finger = 0; finger < kNumFingers; finger++) {
		const Quat<T> *last_orientation = &opt.wrist_final_orientation;
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
		const Vec3<T> *last_translation = &opt.wrist_final_location;
		const Quat<T> *last_orientation = &opt.wrist_final_orientation;
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

float
get_avg_curl_value(const one_frame_input &obs, const int finger)
{
	float avg = 0;
	float sum = 0;
	for (int view = 0; view < 2; view++) {
		const one_frame_one_view &inp = obs.views[view];
		if (!inp.active) {
			continue;
		}
		float conf = 1 / inp.curls[finger].variance; // Can't divide by 0, variance is always >0
		float val = inp.curls[finger].value;
		avg += val * conf;
		sum += conf;
	}
	return avg / sum;
}

HandScalar
calc_stability_curl_multiplier(const OptimizerFinger<HandScalar> &finger_last, HandScalar obs_curl)
{
	HandScalar last_curl_sum = HandScalar(finger_last.proximal_swing.x + finger_last.rots[0] + finger_last.rots[1]);
	//!@todo Use the neural net's output variance somehow
	HandScalar curl_disagreement = abs(obs_curl - last_curl_sum);

	HandScalar curl_sub_mul = 1.0f - curl_disagreement;
	curl_sub_mul += 0.2;


	curl_sub_mul = std::max<HandScalar>(curl_sub_mul, 0);
	curl_sub_mul = std::min<HandScalar>(curl_sub_mul, 1.0);

	return curl_sub_mul;
}

template <typename T>
void
computeResidualStability_Finger(const one_frame_input &observation,
                                const HandStability &stab,
                                const OptimizerHand<T> &hand,
                                const OptimizerHand<HandScalar> &last_hand,
                                int finger_idx,
                                ResidualHelper<T> &helper)
{
	const OptimizerFinger<T> &finger = hand.finger[finger_idx];
	const OptimizerFinger<HandScalar> &finger_last = last_hand.finger[finger_idx];


	HandScalar obs_curl = HandScalar(get_avg_curl_value(observation, finger_idx + 1));
	HandScalar curl_sub_mul = calc_stability_curl_multiplier(finger_last, obs_curl);


	helper.AddValue((finger.proximal_swing.x - finger_last.proximal_swing.x) * //
	                stab.stabilityFingerPXMSwingX * curl_sub_mul);
	helper.AddValue((finger.proximal_swing.y - finger_last.proximal_swing.y) * //
	                stab.stabilityFingerPXMSwingY);

	helper.AddValue((finger.rots[0] - finger_last.rots[0]) * stab.stabilityCurlRoot * curl_sub_mul);
	helper.AddValue((finger.rots[1] - finger_last.rots[1]) * stab.stabilityCurlRoot * curl_sub_mul);

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
                         const KinematicHandLM &state,
                         ResidualHelper<T> &helper)
{
	HandStability stab(state.smoothing_factor);


	if constexpr (optimize_hand_size) {
		helper.AddValue( //
		    (hand.hand_size - state.target_hand_size) * (T)(stab.stabilityHandSize * state.hand_size_err_mul));
	}

	if (state.first_frame) {
		return;
	}

	helper.AddValue((hand.wrist_post_location.x) * stab.stabilityRootPosition);
	helper.AddValue((hand.wrist_post_location.y) * stab.stabilityRootPosition);
	helper.AddValue((hand.wrist_post_location.z) * stab.stabilityRootPosition);


	// Needed because d/dx(sqrt(x)) at x=0 is undefined, and the first iteration *always* starts at 0.
	// x-2sin(0.5x) at x=0.001 is 4.16e-11 - this is a reasonable epsilon to pick.
	const float epsilon = 0.001;
	if (hand.wrist_post_orientation_aax.x < epsilon && //
	    hand.wrist_post_orientation_aax.y < epsilon && //
	    hand.wrist_post_orientation_aax.z < epsilon) {
		helper.AddValue((hand.wrist_post_orientation_aax.x) * (T)(stab.stabilityHandOrientationXY));
		helper.AddValue((hand.wrist_post_orientation_aax.y) * (T)(stab.stabilityHandOrientationXY));
		helper.AddValue((hand.wrist_post_orientation_aax.z) * (T)(stab.stabilityHandOrientationZ));
	} else {
		T rotation_magnitude = hand.wrist_post_orientation_aax.norm();
		T magnitude_sin = T(2) * sin(T(0.5) * rotation_magnitude);

		Vec3<T> rotation_axis = hand.wrist_post_orientation_aax.normalized();


		helper.AddValue((magnitude_sin * rotation_axis.x) * (T)(stab.stabilityHandOrientationXY));
		helper.AddValue((magnitude_sin * rotation_axis.y) * (T)(stab.stabilityHandOrientationXY));
		helper.AddValue((magnitude_sin * rotation_axis.z) * (T)(stab.stabilityHandOrientationZ));
	}



	helper.AddValue((hand.thumb.metacarpal.swing.x - last_hand.thumb.metacarpal.swing.x) *
	                stab.stabilityThumbMCPSwing);
	helper.AddValue((hand.thumb.metacarpal.swing.y - last_hand.thumb.metacarpal.swing.y) *
	                stab.stabilityThumbMCPSwing);
	helper.AddValue((hand.thumb.metacarpal.twist - last_hand.thumb.metacarpal.twist) * stab.stabilityThumbMCPTwist);

	helper.AddValue((hand.thumb.rots[0] - last_hand.thumb.rots[0]) * stab.stabilityCurlRoot);
	helper.AddValue((hand.thumb.rots[1] - last_hand.thumb.rots[1]) * stab.stabilityCurlRoot);
#ifdef USE_HAND_PLAUSIBILITY
	helper.AddValue((hand.finger[1].proximal_swing.x - hand.finger[2].proximal_swing.x) *
	                kPlausibilityProximalSimilarity);
	helper.AddValue((hand.finger[2].proximal_swing.x - hand.finger[3].proximal_swing.x) *
	                kPlausibilityProximalSimilarity);
#endif


	for (int finger_idx = 0; finger_idx < 4; finger_idx++) {
		computeResidualStability_Finger<T>(*state.observation, stab, hand, last_hand, finger_idx, helper);
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
	Vec3<T> vec = {
	    (T)(in.x),
	    (T)(in.y),
	    (T)(in.z),
	};

	normalize_vector_inplace(vec);

	unit_vector_stereographic_projection(vec, out);
}

template <typename T>
static void
calc_joint_rel_camera(const Vec3<T> &model_joint_pos,
                      const Vec3<T> &move_joint_translation,
                      const Quat<T> &move_joint_orientation,
                      const Quat<T> &after_orientation,
                      Vec3<T> &out_position)
{
	// Should be uninitialized until here.
	out_position = Vec3<T>::Zero();
	UnitQuaternionRotatePoint(move_joint_orientation, model_joint_pos, out_position);
	out_position.x += move_joint_translation.x;
	out_position.y += move_joint_translation.y;
	out_position.z += move_joint_translation.z;

	UnitQuaternionRotatePoint(after_orientation, out_position, out_position);
}

template <typename T>
static void
diff_stereographic(const Vec3<T> &model_joint_pos_rel_camera_,
                   const vec2_5 &observed_ray_sg,
                   const HandScalar confidence_xy,
                   const HandScalar stereographic_radius,
                   ResidualHelper<T> &helper)
{
	Vec3<T> model_joint_pos_rel_camera = model_joint_pos_rel_camera_;
	normalize_vector_inplace(model_joint_pos_rel_camera);
	Vec2<T> stereographic_model_dir;
	unit_vector_stereographic_projection(model_joint_pos_rel_camera, stereographic_model_dir);

	helper.AddValue((stereographic_model_dir.x - (T)(observed_ray_sg.pos_2d.x * stereographic_radius)) *
	                confidence_xy);
	helper.AddValue((stereographic_model_dir.y - (T)(observed_ray_sg.pos_2d.y * stereographic_radius)) *
	                confidence_xy);
}



template <typename T>
void
cjrc(const KinematicHandLM &state,                   //
     const OptimizerHand<T> &hand,                   //
     const Translations55<T> &translations_absolute, //
     const int view,                                 //
     Vec3<T> out_model_joints_rel_camera[21])
{
	Vec3<T> move_direction;
	Quat<T> move_orientation;

	Quat<T> after_orientation;

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



	xrt_quat extra_rot = state.observation->views[view].look_dir;


#if 1
	math_quat_invert(&extra_rot, &extra_rot);
#endif


	after_orientation.w = T(extra_rot.w);
	after_orientation.x = T(extra_rot.x);
	after_orientation.y = T(extra_rot.y);
	after_orientation.z = T(extra_rot.z);


	int joint_acc_idx = 0;

	Vec3<T> root = state.this_frame_pre_position;
	root.x += hand.wrist_post_location.x;
	root.y += hand.wrist_post_location.y;
	root.z += hand.wrist_post_location.z;


	calc_joint_rel_camera(root, move_direction, move_orientation, after_orientation,
	                      out_model_joints_rel_camera[joint_acc_idx++]);

	for (int finger_idx = 0; finger_idx < 5; finger_idx++) {
		for (int joint_idx = 0; joint_idx < 4; joint_idx++) {
			calc_joint_rel_camera(translations_absolute.t[finger_idx][joint_idx + 1], move_direction,
			                      move_orientation, after_orientation,
			                      out_model_joints_rel_camera[joint_acc_idx++]);
		}
	}
}


template <typename T>
void
CostFunctor_PositionsPart(const OptimizerHand<T> &hand,
                          const Translations55<T> &translations_absolute,
                          //   Orientations54<T> &orientations_absolute,
                          const KinematicHandLM &state,
                          ResidualHelper<T> &helper)
{
	for (int view = 0; view < 2; view++) {
		if (!state.observation->views[view].active) {
			continue;
		}
		HandScalar stereographic_radius = state.observation->views[view].stereographic_radius;
		Vec3<T> model_joints_rel_camera[21] = {};

		cjrc(state, hand, translations_absolute, view, model_joints_rel_camera);
		MLOutput2D &out = state.observation->views[view].keypoints_in_scaled_stereographic;

		T middlepxmdepth = model_joints_rel_camera[Joint21::INDX_PXM].norm();

		for (int i = 0; i < 21; i++) {

			diff_stereographic<T>(model_joints_rel_camera[i],                                          //
			                      state.observation->views[view].keypoints_in_scaled_stereographic[i], //
			                      out[i].confidence_xy,                                                //
			                      stereographic_radius,                                                //
			                      helper);


			if (i == Joint21::MIDL_PXM) {
				continue;
			}
			// depth part!
			T rel_depth = model_joints_rel_camera[i].norm() - middlepxmdepth;
			rel_depth /= hand.hand_size;



			T obs_depth = T(out[i].depth_relative_to_midpxm);

			T relative_diff = rel_depth - obs_depth;

			if (state.first_frame) {
				// Depth on the first frame was causing local minima. We need simulated annealing.
				helper.AddValue(T(0));
			} else {
				helper.AddValue(relative_diff * T(pow(out[i].confidence_depth, 3)) * T(1.0f) *
				                state.depth_err_mul);
			}
		}
	}
}

template <typename T>
static void
diff_stereographic_reprojection_error(const Vec3<T> &model_joint_pos_rel_camera_,
                                      const vec2_5 &observed_ray_sg,
                                      const HandScalar confidence_xy,
                                      const HandScalar stereographic_radius,
                                      ResidualHelper<T> &helper)
{
	Vec3<T> model_joint_pos_rel_camera = model_joint_pos_rel_camera_;
	normalize_vector_inplace(model_joint_pos_rel_camera);
	Vec2<T> stereographic_model_dir;
	unit_vector_stereographic_projection(model_joint_pos_rel_camera, stereographic_model_dir);

	stereographic_model_dir.x /= stereographic_radius;
	stereographic_model_dir.y /= stereographic_radius;

	//!@todo This works well but we can get a way more "rooted in math" way of increasing repro error with
	//! low-confidence measurements than this.
	HandScalar mul = 1 / (0.2 + confidence_xy);

	helper.AddValue((stereographic_model_dir.x - (T)(observed_ray_sg.pos_2d.x)) * mul);
	helper.AddValue((stereographic_model_dir.y - (T)(observed_ray_sg.pos_2d.y)) * mul);
}

// A much simpler reprojection error function for evaluating the final "goodness" so we can drop badly optimized hands.
template <typename T>
void
simple_reprojection_error(const OptimizerHand<T> &hand,
                          const Translations55<T> &translations_absolute,
                          const Orientations54<T> &orientations_absolute,
                          const KinematicHandLM &state,

                          ResidualHelper<T> &helper)
{


	for (int view = 0; view < 2; view++) {
		if (!state.observation->views[view].active) {
			continue;
		}

		HandScalar stereographic_radius = state.observation->views[view].stereographic_radius;
		Vec3<T> model_joints_rel_camera[21] = {};
		cjrc(state, hand, translations_absolute, view, model_joints_rel_camera);

		for (int i = 0; i < 21; i++) {
			diff_stereographic_reprojection_error(
			    model_joints_rel_camera[i],                                          //
			    state.observation->views[view].keypoints_in_scaled_stereographic[i], //
			    1.0f,                                                                //
			    stereographic_radius,                                                //
			    helper);
		}
	}
}

#ifdef USE_HAND_CURLS
template <typename T>
void
CostFunctor_MatchCurls(OptimizerHand<T> &hand, KinematicHandLM &state, ResidualHelper<T> &helper)
{
	for (int view = 0; view < 2; view++) {
		one_frame_one_view &inp = state.observation->views[view];
		if (!inp.active) {
			continue;
		}

		for (int finger = 0; finger < 4; finger++) {
			OptimizerFinger<T> fing = hand.finger[finger];

			T sum = fing.proximal_swing.x + fing.rots[0] + fing.rots[1];

			T target = T(inp.curls[finger + 1].value);

			T diff = (sum - target) * T(1 / inp.curls[finger + 1].variance);

			helper.AddValue(diff);
		}
	}
}
#endif


template <typename T>
void
print_residual_part(T *residual, int residual_size)
{
	for (int i = 0; i < residual_size; i++) {
		std::cout << residual[i] << std::endl;
	}
}

template <bool optimize_hand_size>
template <typename T>
bool
CostFunctor<optimize_hand_size>::operator()(const T *const x, T *residual) const
{

	struct KinematicHandLM &state = this->parent;
	OptimizerHand<T> hand = {};
	// ??? should I do the below? probably.
	Quat<T> tmp = this->parent.this_frame_pre_rotation;
	OptimizerHandInit<T>(hand, tmp);
	OptimizerHandUnpackFromVector(x, state, hand);

	XRT_MAYBE_UNUSED size_t residual_size =
	    calc_residual_size(state.use_stability, optimize_hand_size, state.num_observation_views);

// When you're hacking, you want to set the residuals to always-0 so that any of them you forget to touch keep their 0
// gradient.
#ifdef RESIDUALS_HACKING
	for (size_t i = 0; i < residual_size; i++) {
		residual[i] = (T)(0);
	}
#endif


	ResidualHelper<T> helper(residual);


	Translations55<T> translations_absolute = {};
	Orientations54<T> orientations_absolute = {};
	eval_hand_with_orientation(state, hand, state.is_right, translations_absolute, orientations_absolute);


	CostFunctor_PositionsPart<T>(hand, translations_absolute, state, helper);
	computeResidualStability<optimize_hand_size, T>(hand, state.last_frame, state, helper);

#ifdef USE_HAND_CURLS
	CostFunctor_MatchCurls<T>(hand, state, helper);
#endif

// Bounds checking - we should have written exactly to the end.
// If you're hacking on the optimizer, just increase the residual size a lot and don't worry.
#ifndef RESIDUALS_HACKING
	if (helper.out_residual_idx != residual_size) {
		LM_ERROR(state, "Residual size was wrong! Residual size was %zu, but out_residual_idx was %zu",
		         residual_size, helper.out_residual_idx);
	}
	assert(helper.out_residual_idx == residual_size);
#endif

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
eval_to_viz_hand(KinematicHandLM &state,
                 OptimizerHand<HandScalar> &opt,
                 Translations55<HandScalar> translations_absolute,
                 Orientations54<HandScalar> orientations_absolute,
                 xrt_hand_joint_set &out_viz_hand)
{
	XRT_TRACE_MARKER();

	eval_hand_with_orientation(state, opt, state.is_right, translations_absolute, orientations_absolute);

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
	zldtt(opt.wrist_final_location, opt.wrist_final_orientation, state.is_right,
	      out_viz_hand.values.hand_joint_set_default[joint_acc_idx++].relation);

	for (int finger = 0; finger < 5; finger++) {
		for (int joint = 0; joint < 5; joint++) {
			// This one is necessary
			if (finger == 0 && joint == 0) {
				continue;
			}
			Quat<HandScalar> *orientation = nullptr;
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

	ceres::TinySolver<AutoDiffCostFunctor> solver = {};
	solver.options.max_num_iterations = 30;

	//!@todo We don't yet know what "good" termination conditions are.
	// Instead of trying to guess without good offline datasets, just disable _all_ termination conditions and have
	// it run for 30 iterations no matter what.
	solver.options.gradient_tolerance = 0;
	solver.options.function_tolerance = 0;
	solver.options.parameter_tolerance = 0;

	//!@todo We need to do a parameter sweep on initial_trust_region_radius.

	Eigen::Matrix<HandScalar, input_size, 1> inp = state.TinyOptimizerInput.head<input_size>();

	XRT_MAYBE_UNUSED uint64_t start = os_monotonic_get_ns();
	XRT_MAYBE_UNUSED auto summary = solver.Solve(f, &inp);
	XRT_MAYBE_UNUSED uint64_t end = os_monotonic_get_ns();

	//!@todo Is there a zero-copy way of doing this?
	state.TinyOptimizerInput.head<input_size>() = inp;

	if (state.log_level <= U_LOGGING_DEBUG) {

		uint64_t diff = end - start;
		double time_taken = (double)diff / (double)U_TIME_1MS_IN_NS;

		const char *status = nullptr;

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
		default: assert(false);
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
optimizer_finish(KinematicHandLM &state, xrt_hand_joint_set &out_viz_hand, float &out_reprojection_error)
{

	// Postfix - unpack,

	OptimizerHand<HandScalar> &final_hand = state.last_frame;

	Translations55<HandScalar> translations_absolute;
	Orientations54<HandScalar> orientations_absolute;
	eval_hand_with_orientation<HandScalar>(state, final_hand, state.is_right, translations_absolute,
	                                       orientations_absolute);

	eval_to_viz_hand(state, final_hand, translations_absolute, orientations_absolute, out_viz_hand);


	// CALCULATE REPROJECTION ERROR

	// Let's make space calc_residual_sizefor two views, even though we may only use one.
	Eigen::Matrix<HandScalar, kHandResidualOneSideXY * 2, 1> mat; //= mat.Zero();
	mat.setConstant(0);

	ResidualHelper<HandScalar> helper(mat.data());



	simple_reprojection_error<HandScalar>(final_hand, translations_absolute, orientations_absolute, state, helper);

	HandScalar sum = mat.squaredNorm();

	sum /= state.num_observation_views;

	out_reprojection_error = sum;
}

void
optimizer_run(KinematicHandLM *hand,
              one_frame_input &observation,
              bool hand_was_untracked_last_frame,
              float smoothing_factor, //!<- Unused if this is the first frame
              bool optimize_hand_size,
              float target_hand_size,
              float hand_size_err_mul,
              float amt_use_depth,
              xrt_hand_joint_set &out_viz_hand,
              float &out_hand_size,
              float &out_reprojection_error) // NOLINT(bugprone-easily-swappable-parameters)
{
	numerics_checker::set_floating_exceptions();

	KinematicHandLM &state = *hand;
	state.smoothing_factor = smoothing_factor;

	xrt_pose blah = XRT_POSE_IDENTITY;
	hand_init_guess(observation, target_hand_size, state.left_in_right, blah);

	if (hand_was_untracked_last_frame) {
		OptimizerHandInit(state.last_frame, state.this_frame_pre_rotation);
		OptimizerHandPackIntoVector(state.last_frame, state.optimize_hand_size,
		                            state.TinyOptimizerInput.data());
		if (blah.position.z > 0.05) {
			LM_WARN(state, "Initial position guess was too close to camera! Z axis was %f m",
			        blah.position.z);
			state.this_frame_pre_position.x = 0.0f;
			state.this_frame_pre_position.y = 0.0f;
			state.this_frame_pre_position.z = -0.3f;
		} else {
			state.this_frame_pre_position.x = blah.position.x;
			state.this_frame_pre_position.y = blah.position.y;
			state.this_frame_pre_position.z = blah.position.z;
		}



		state.this_frame_pre_rotation.x = blah.orientation.x;
		state.this_frame_pre_rotation.y = blah.orientation.y;
		state.this_frame_pre_rotation.z = blah.orientation.z;
		state.this_frame_pre_rotation.w = blah.orientation.w;
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
	state.depth_err_mul = amt_use_depth;

	state.use_stability = !state.first_frame;

	state.observation = &observation;



	// This code is disabled because I can't convince myself that it helps (I will be able to once we have good
	// validation datasets.)

	// What it does: Update each finger's "initial" curl value to match what the neural net thought the curl was,
	// so that the optimizer hopefully starts in the valley that contains the true global minimum.
#if 0

	OptimizerHand<HandScalar> blah = {};
	OptimizerHandUnpackFromVector(state.TinyOptimizerInput.data(), state, blah);

	for (int finger_idx = 0; finger_idx < 4; finger_idx++) {
		// int finger_idx = 0;
		int curl_idx = finger_idx + 1;


		OptimizerFinger fing = blah.finger[finger_idx];
		HandScalar sum = fing.proximal_swing.x + fing.rots[0] + fing.rots[1];

		HandScalar target = get_avg_curl_value(observation, curl_idx);


		HandScalar move = target - sum;

		fing.proximal_swing.x += move / 3;
		fing.rots[0] += move / 3;
		fing.rots[1] += move / 3;

	}

	OptimizerHandPackIntoVector(blah, state.optimize_hand_size, state.TinyOptimizerInput.data());



#endif


	// For now, we have to statically instantiate different versions of the optimizer depending on
	// how many input parameters there are. For now, there are only two cases - either we are
	// optimizing the hand size or we are not optimizing it.
	// !@todo Can we make a magic template that automatically instantiates the right one, and also
	// make it so we can decide to either make the residual size dynamic or static? Currently, it's
	// dynamic, which is easier for us and makes compile times a lot lower, but it probably makes
	// things some amount slower at runtime.

	if (optimize_hand_size) {
		opt_run<true>(state, observation, out_viz_hand);
	} else {
		opt_run<false>(state, observation, out_viz_hand);
	}



	// Postfix - unpack our optimization result into state.last_frame.
	OptimizerHandUnpackFromVector(state.TinyOptimizerInput.data(), state, state.last_frame);



	// Have the final pose from this frame be the next frame's initial pose
	state.this_frame_pre_rotation = state.last_frame.wrist_final_orientation;
	state.this_frame_pre_position = state.last_frame.wrist_final_location;

	// Reset this frame's post-transform to identity
	state.last_frame.wrist_post_location.x = 0.0f;
	state.last_frame.wrist_post_location.y = 0.0f;
	state.last_frame.wrist_post_location.z = 0.0f;

	state.last_frame.wrist_post_orientation_aax.x = 0.0f;
	state.last_frame.wrist_post_orientation_aax.y = 0.0f;
	state.last_frame.wrist_post_orientation_aax.z = 0.0f;

	// Repack - brings the curl values back into original domain. Look at ModelToLM/LMToModel, we're
	// using sin/asin.
	OptimizerHandPackIntoVector(state.last_frame, hand->optimize_hand_size, state.TinyOptimizerInput.data());

	optimizer_finish(state, out_viz_hand, out_reprojection_error);

	state.first_frame = false;

	out_hand_size = state.last_frame.hand_size;
	numerics_checker::remove_floating_exceptions();
}



void
optimizer_create(xrt_pose left_in_right, bool is_right, u_logging_level log_level, KinematicHandLM **out_kinematic_hand)
{
	KinematicHandLM *hand = new KinematicHandLM();

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

	*out_kinematic_hand = hand;
}

void
optimizer_destroy(KinematicHandLM **hand)
{
	delete *hand;
	hand = NULL;
}
} // namespace xrt::tracking::hand::mercury::lm
