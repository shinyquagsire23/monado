#pragma once

// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Helper math to do things with 3D hands for the camera-based hand tracker
 * @author Moses Turner <moses@collabora.com>
 * @author Nick Klingensmith <programmerpichu@gmail.com>
 * @ingroup drv_ht
 */

#include "math/m_api.h"
#include "math/m_vec3.h"

#include "rgb_sync.hpp"
#include "util/u_time.h"
#include "xrt/xrt_defines.h"

static constexpr int num_real_joints = 21;

float
sumOfHandJointDistances(const Hand3D &one, const Hand3D &two)
{
	float dist = 0.0f;
	for (int i = 0; i < num_real_joints; i++) {
		dist += m_vec3_len(one.kps[i] - two.kps[i]);
	}
	return dist;
}

float
errHandHistory(const HandHistory3D &history_hand, const Hand3D &present_hand)
{
	// Remember we never have to deal with an empty hand. Can always read the last element.
	return sumOfHandJointDistances(history_hand.last_hands_unfiltered.back(), present_hand);
}

float
errHandDisparity(const Hand2D &left_rays, const Hand2D &right_rays)
{
	float error_y_diff = 0.0f;
	for (int i = 0; i < 21; i++) {
		float diff_y = fabsf(left_rays.kps[i].y - right_rays.kps[i].y);
		// Big question about what's the best loss function. Gut feeling was "I should be using sum of squared
		// errors" but I don't really know. Using just sum of errors for now. Ideally it'd also be not very
		// sensitive to one or two really bad outliers.
		error_y_diff += diff_y;
	}
	// U_LOG_E("stereo camera err is %f, y_disparity is %f", err_stereo_camera, error_y_diff);
	return error_y_diff;
}

void
applyThumbIndexDrag(Hand3D *hand)
{
	// TERRIBLE HACK.
	// Puts the thumb and pointer a bit closer together to be better at triggering XR clients' pinch detection.
	static const float max_radius = 0.05;
	static const float min_radius = 0.00;

	// no min drag, min drag always 0.
	static const float max_drag = 0.85f;

	xrt_vec3 thumb = hand->kps[THMB_TIP];
	xrt_vec3 index = hand->kps[INDX_TIP];
	xrt_vec3 ttp = index - thumb;
	float length = m_vec3_len(ttp);
	if ((length > max_radius)) {
		return;
	}


	float amount = math_map_ranges(length, min_radius, max_radius, max_drag, 0.0f);

	hand->kps[THMB_TIP] = m_vec3_lerp(thumb, index, amount * 0.5f);
	hand->kps[INDX_TIP] = m_vec3_lerp(index, thumb, amount * 0.5f);
}

static inline xrt_vec3
get_joint_position(struct xrt_hand_joint_set *set, xrt_hand_joint jt)
{
	return set->values.hand_joint_set_default[jt].relation.pose.position;
}

template <size_t N>
static inline void
set_finger(struct xrt_hand_joint_set *set,
           const xrt_vec3 &pinky_to_index_prox,
           const std::array<xrt_hand_joint, N> &finger)
{
	for (size_t i = 0; i < N - 1; i++) {
		// Don't do fingertips. (Fingertip would be index 4.)
		struct xrt_vec3 forwards =
		    m_vec3_normalize(get_joint_position(set, finger[i + 1]) - get_joint_position(set, finger[i]));
		struct xrt_vec3 backwards = m_vec3_mul_scalar(forwards, -1.0f);

		struct xrt_vec3 left = m_vec3_orthonormalize(forwards, pinky_to_index_prox);
		// float dot = m_vec3_dot(backwards, left);
		// assert((m_vec3_dot(backwards,left) == 0.0f));
		math_quat_from_plus_x_z(&left, &backwards,
		                        &set->values.hand_joint_set_default[finger[i]].relation.pose.orientation);
	}
	// Do fingertip! Per XR_EXT_hand_tracking, just copy the distal joint's orientation. Doing anything else
	// is wrong.
	set->values.hand_joint_set_default[finger[N - 1]].relation.pose.orientation =
	    set->values.hand_joint_set_default[finger[N - 2]].relation.pose.orientation;
}

void
applyJointOrientations(struct xrt_hand_joint_set *set, bool is_right)
{
	// The real rule to follow is that each joint's "X" axis is along the axis along which it can bend.
	// The nature of our estimation makes this a bit difficult, but these should work okay-ish under perfect
	// conditions
	if (set->is_active == false) {
		return;
	}

	auto gl = [&](xrt_hand_joint jt) { return get_joint_position(set, jt); };

	xrt_vec3 pinky_prox = gl(XRT_HAND_JOINT_LITTLE_PROXIMAL);

	xrt_vec3 index_prox = gl(XRT_HAND_JOINT_INDEX_PROXIMAL);


	xrt_vec3 pinky_to_index_prox = m_vec3_normalize(index_prox - pinky_prox);
	if (is_right) {
		pinky_to_index_prox = m_vec3_mul_scalar(pinky_to_index_prox, -1.0f);
	}

	using Finger = std::array<xrt_hand_joint, 5>;
	static const std::array<Finger, 4> fingers_with_joints_in_them = {{

	    {XRT_HAND_JOINT_INDEX_METACARPAL, XRT_HAND_JOINT_INDEX_PROXIMAL, XRT_HAND_JOINT_INDEX_INTERMEDIATE,
	     XRT_HAND_JOINT_INDEX_DISTAL, XRT_HAND_JOINT_INDEX_TIP},

	    {XRT_HAND_JOINT_MIDDLE_METACARPAL, XRT_HAND_JOINT_MIDDLE_PROXIMAL, XRT_HAND_JOINT_MIDDLE_INTERMEDIATE,
	     XRT_HAND_JOINT_MIDDLE_DISTAL, XRT_HAND_JOINT_MIDDLE_TIP},

	    {XRT_HAND_JOINT_RING_METACARPAL, XRT_HAND_JOINT_RING_PROXIMAL, XRT_HAND_JOINT_RING_INTERMEDIATE,
	     XRT_HAND_JOINT_RING_DISTAL, XRT_HAND_JOINT_RING_TIP},

	    {XRT_HAND_JOINT_LITTLE_METACARPAL, XRT_HAND_JOINT_LITTLE_PROXIMAL, XRT_HAND_JOINT_LITTLE_INTERMEDIATE,
	     XRT_HAND_JOINT_LITTLE_DISTAL, XRT_HAND_JOINT_LITTLE_TIP},

	}};
	for (Finger const &finger : fingers_with_joints_in_them) {
		set_finger(set, pinky_to_index_prox, finger);
	}

	// wrist!
	// Not the best but acceptable. Eventually, probably, do triangle of wrist pinky prox and index prox.
	set->values.hand_joint_set_default[XRT_HAND_JOINT_WRIST].relation.pose.orientation =
	    set->values.hand_joint_set_default[XRT_HAND_JOINT_MIDDLE_METACARPAL].relation.pose.orientation;


	// palm!
	set->values.hand_joint_set_default[XRT_HAND_JOINT_PALM].relation.pose.orientation =
	    set->values.hand_joint_set_default[XRT_HAND_JOINT_MIDDLE_METACARPAL].relation.pose.orientation;

	// thumb!
	// When I look at Ultraleap tracking, there's like, a "plane" made by the tip, distal and proximal (and kinda
	// MCP, but least squares fitting a plane is too hard for my baby brain) Normal to this plane is the +X, and
	// obviously forwards to the next joint is the -Z.
	xrt_vec3 thumb_prox_to_dist = gl(XRT_HAND_JOINT_THUMB_DISTAL) - gl(XRT_HAND_JOINT_THUMB_PROXIMAL);
	xrt_vec3 thumb_dist_to_tip = gl(XRT_HAND_JOINT_THUMB_TIP) - gl(XRT_HAND_JOINT_THUMB_DISTAL);
	xrt_vec3 plane_normal{};
	if (!is_right) {
		math_vec3_cross(&thumb_prox_to_dist, &thumb_dist_to_tip, &plane_normal);
	} else {
		math_vec3_cross(&thumb_dist_to_tip, &thumb_prox_to_dist, &plane_normal);
	}
	constexpr std::array<enum xrt_hand_joint, 4> thumbs = {XRT_HAND_JOINT_THUMB_METACARPAL,
	                                                       XRT_HAND_JOINT_THUMB_PROXIMAL,
	                                                       XRT_HAND_JOINT_THUMB_DISTAL, XRT_HAND_JOINT_THUMB_TIP};
	//! @todo this code isn't quite the same as set_finger, can we make it the same so we can use that?
	for (int i = 0; i < 3; i++) {
		struct xrt_vec3 backwards =
		    m_vec3_mul_scalar(m_vec3_normalize(gl(thumbs[i + 1]) - gl(thumbs[i])), -1.0f);

		struct xrt_vec3 left = m_vec3_orthonormalize(backwards, plane_normal);
		math_quat_from_plus_x_z(&left, &backwards,
		                        &set->values.hand_joint_set_default[thumbs[i]].relation.pose.orientation);
	}
	struct xrt_quat *tip = &set->values.hand_joint_set_default[XRT_HAND_JOINT_THUMB_TIP].relation.pose.orientation;
	struct xrt_quat *distal =
	    &set->values.hand_joint_set_default[XRT_HAND_JOINT_THUMB_DISTAL].relation.pose.orientation;
	memcpy(tip, distal, sizeof(struct xrt_quat));
}

float
handednessJointSet(Hand3D *set)
{
	// Guess if hand is left or right.
	// Left is negative, right is positive.


	// xrt_vec3 middle_mcp = gl(XRT_HAND_JOINT_MIDDLE_METACARPAL);

	xrt_vec3 pinky_prox = set->kps[LITL_PXM]; // gl(XRT_HAND_JOINT_LITTLE_PROXIMAL);

	xrt_vec3 index_prox = set->kps[INDX_PXM]; // gl(XRT_HAND_JOINT_INDEX_PROXIMAL);

	xrt_vec3 pinky_to_index_prox = m_vec3_normalize(index_prox - pinky_prox);

	float handedness = 0.0f;

	for (int i : {INDX_PXM, MIDL_PXM, RING_PXM, LITL_PXM}) {
		xrt_vec3 prox = set->kps[i];
		xrt_vec3 intr = set->kps[i + 1];
		xrt_vec3 dist = set->kps[i + 2];
		xrt_vec3 tip = set->kps[i + 3];

		xrt_vec3 prox_to_int = m_vec3_normalize(intr - prox);
		xrt_vec3 int_to_dist = m_vec3_normalize(dist - intr);
		xrt_vec3 dist_to_tip = m_vec3_normalize(tip - dist);

		xrt_vec3 checks[2];

		math_vec3_cross(&prox_to_int, &int_to_dist, &checks[0]);
		math_vec3_cross(&int_to_dist, &dist_to_tip, &checks[1]);

		handedness += m_vec3_dot(m_vec3_normalize(pinky_to_index_prox), (checks[0]));
		handedness += m_vec3_dot(m_vec3_normalize(pinky_to_index_prox), (checks[1]));
	}
	set->handedness = handedness / (4 * 2);
	return set->handedness;
}

void
handednessHandHistory3D(HandHistory3D *history)
{

	float inter = handednessJointSet(&history->last_hands_unfiltered.back());

	if ((fabsf(inter) > 0.3f) || (fabsf(history->handedness) < 0.3f)) {
		history->handedness += inter;
	}
	static const int max_handedness = 2.0f;
	if (history->handedness > max_handedness) {
		history->handedness = max_handedness;
	} else if (history->handedness < -max_handedness) {
		history->handedness = -max_handedness;
	}
}

void
handEuroFiltersInit(HandHistory3D *history, double fc_min, double fc_min_d, double beta)
{
	for (int i = 0; i < 21; i++) {
		m_filter_euro_vec3_init(&history->filters[i], fc_min, fc_min_d, beta);
	}
}

static double
calc_smoothing_alpha(double Fc, double dt)
{
	/* Calculate alpha = (1 / (1 + tau/dt)) where tau = 1.0 / (2 * pi * Fc),
	 * this is a straight rearrangement with fewer divisions */
	double r = 2.0 * M_PI * Fc * dt;
	return r / (r + 1.0);
}

static double
exp_smooth(double alpha, double y, double prev_y)
{
	return alpha * y + (1.0 - alpha) * prev_y;
}

void
handEuroFiltersRun(struct HandTracking *htd, HandHistory3D *f, Hand3D *out_hand)
{
	// Assume present hand is in element 0!
#if 0
	// float vals[4] = {0.5, 0.33, 0.1, 0.07};
	float vals[4] = {0.9, 0.09, 0.009, 0.001};
	auto m = f->last_hands_unfiltered.size() - 1;
	double ts_out = (vals[0] * (double)f->last_hands_unfiltered.get_at_age(std::min(m, 0))->timestamp) +
	                (vals[1] * (double)f->last_hands_unfiltered.get_at_age(std::min(m, 1))->timestamp) +
	                (vals[2] * (double)f->last_hands_unfiltered.get_at_age(std::min(m, 2))->timestamp) +
	                (vals[3] * (double)f->last_hands_unfiltered.get_at_age(std::min(m, 3))->timestamp);
	out_hand->timestamp = (uint64_t)ts_out;

	for (int kp_idx = 0; kp_idx < 21; kp_idx++) {
		for (int hist_idx = 0; hist_idx < 4; hist_idx++) {
			float *in_y_arr =
			    (float *)&f->last_hands_unfiltered.get_at_age(std::min(m, hist_idx))->kps[kp_idx];
			float *out_y_arr = (float *)&out_hand->kps[kp_idx];
			for (int i = 0; i < 3; i++) {
				out_y_arr[i] += in_y_arr[i] * vals[hist_idx];
			}
		}
	}
#elif 0
	for (int i = 0; i < 21; i++) {
		m_filter_euro_vec3_run(&f->filters[i], f->last_hands_unfiltered.back().timestamp,
		                       &f->last_hands_unfiltered.back().kps[i], &out_hand->kps[i]);
	}
	// conspicuously wrong!
	out_hand->timestamp = f->last_hands_unfiltered.back().timestamp;
#else

	if (!f->have_prev_hand) {
		f->last_hands_filtered.push_back(f->last_hands_unfiltered.back());
		uint64_t ts = f->last_hands_unfiltered.back().timestamp;
		f->prev_ts_for_alpha = ts;
		f->first_ts = ts;
		f->prev_filtered_ts = ts;
		f->prev_dy = 0;
		f->have_prev_hand = true;
		*out_hand = f->last_hands_unfiltered.back();
	}
	uint64_t ts = f->last_hands_unfiltered.back().timestamp;
	double dt, alpha_d;
	dt = (double)(ts - f->prev_ts_for_alpha) / U_TIME_1S_IN_NS;

	double abs_dy =
	    (sumOfHandJointDistances(f->last_hands_unfiltered.back(), f->last_hands_filtered.back()) / 21.0f) * 0.7f;
	alpha_d = calc_smoothing_alpha(htd->dynamic_config.hand_fc_min_d.val, dt);

	double alpha, fc_cutoff;
	f->prev_dy = exp_smooth(alpha_d, abs_dy, f->prev_dy);

	fc_cutoff = htd->dynamic_config.hand_fc_min.val + htd->dynamic_config.hand_beta.val * f->prev_dy;
	alpha = calc_smoothing_alpha(fc_cutoff, dt);
	HT_DEBUG(htd, "dt is %f, abs_dy is %f, alpha is %f", dt, abs_dy, alpha);

	for (int i = 0; i < 21; i++) {
		out_hand->kps[i].x =
		    exp_smooth(alpha, f->last_hands_unfiltered.back().kps[i].x, f->last_hands_filtered.back().kps[i].x);
		out_hand->kps[i].y =
		    exp_smooth(alpha, f->last_hands_unfiltered.back().kps[i].y, f->last_hands_filtered.back().kps[i].y);
		out_hand->kps[i].z =
		    exp_smooth(alpha, f->last_hands_unfiltered.back().kps[i].z, f->last_hands_filtered.back().kps[i].z);
	}
	double prev_ts_offset = (double)(f->prev_filtered_ts - f->first_ts);
	double current_ts_offset = (double)(ts - f->first_ts);
	double new_filtered_ts_offset = exp_smooth(alpha, current_ts_offset, prev_ts_offset);
	uint64_t new_filtered_ts = (uint64_t)(new_filtered_ts_offset) + f->first_ts;
	out_hand->timestamp = new_filtered_ts;
	f->prev_filtered_ts = out_hand->timestamp;
	f->prev_ts_for_alpha = ts; // NOT the filtered timestamp. NO.
#endif
}

bool
rejectTooFar(struct HandTracking *htd, Hand3D *hand)
{
	static const float max_dist = 1.0f; // this sucks too - make it bigger if you can.
	const float max_dist_from_camera_sqrd = max_dist * max_dist;
	for (int i = 0; i < 21; i++) {
		xrt_vec3 pos = hand->kps[i];
		float len = m_vec3_len_sqrd(pos); // Faster.
		if (len > max_dist_from_camera_sqrd) {
			goto reject;
		}
	}
	return true;

reject:
	HT_TRACE(htd, "Rejected too far!");
	return false;
}

bool
rejectTooClose(struct HandTracking *htd, Hand3D *hand)
{
	const float min_dist = 0.12f; // Be a bit aggressive here - it's nice to not let people see our tracking fail
	                              // when the hands are way too close
	const float min_dist_from_camera_sqrd = min_dist * min_dist;

	for (int i = 0; i < 21; i++) {
		xrt_vec3 pos = hand->kps[i];
		float len = m_vec3_len_sqrd(pos); // Faster.
		if (len < min_dist_from_camera_sqrd) {
			goto reject;
		}
		if (pos.z > min_dist) { // remember negative-Z is forward!
			goto reject;
		}
	}
	return true;

reject:
	HT_TRACE(htd, "Rejected too close!");
	return false;
}

bool
rejectTinyPalm(struct HandTracking *htd, Hand3D *hand)
{
	// This one sucks, because some people really have tiny hands. If at some point you can stop using it, stop
	// using it.
	// Weird scoping so that we can still do gotos

	{
		float len = m_vec3_len(hand->kps[WRIST] - hand->kps[INDX_PXM]);
		if ((len < 0.03f || len > 0.25f)) {
			goto reject;
		}
	}

	{
		float len = m_vec3_len(hand->kps[WRIST] - hand->kps[MIDL_PXM]);
		if (len < 0.03f || len > 0.25f) {
			goto reject;
		}
	}

	return true;

reject:
	HT_TRACE(htd, "Rejected because too big or too small!");
	return false;
}
