// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Mercury hand tracking main file.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @author Charlton Rodda <charlton.rodda@collabora.com>
 * @ingroup tracking
 */

#include "hg_sync.hpp"
#include "hg_image_math.inl"
#include "util/u_box_iou.hpp"
#include "util/u_hand_tracking.h"
#include "math/m_vec2.h"
#include "util/u_misc.h"
#include "xrt/xrt_frame.h"


#include <numeric>


namespace xrt::tracking::hand::mercury {
#define DEG_TO_RAD(DEG) (DEG * M_PI / 180.)

DEBUG_GET_ONCE_LOG_OPTION(mercury_log, "MERCURY_LOG", U_LOGGING_WARN)
DEBUG_GET_ONCE_BOOL_OPTION(mercury_optimize_hand_size, "MERCURY_optimize_hand_size", true)

// Flags to tell state tracker that these are indeed valid joints
static const enum xrt_space_relation_flags valid_flags_ht = (enum xrt_space_relation_flags)(
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

/*!
 * Setup helper functions.
 */

static bool
getCalibration(struct HandTracking *hgt, t_stereo_camera_calibration &calibration)
{
	xrt_vec3 trans = {
	    (float)calibration.camera_translation[0],
	    (float)calibration.camera_translation[1],
	    (float)calibration.camera_translation[2],
	};

	if (hgt->log_level <= U_LOGGING_DEBUG) {
		HG_DEBUG(hgt, "Dumping full camera calibration!");
		t_stereo_camera_calibration_dump(&calibration);
	}

	hgt->baseline = m_vec3_len(trans);
	HG_DEBUG(hgt, "I think the baseline is %f meters!", hgt->baseline);

	{
		// Officially, I have no idea if this is row-major or col-major. Empirically it seems to work, and that
		// is all I will say.

		// It might be that the below is *transposing* the matrix, I never remember if OpenCV's R is "left in
		// right" or "right in left"
		//!@todo
		xrt_matrix_3x3 s;

		s.v[0] = (float)calibration.camera_rotation[0][0];
		s.v[1] = (float)calibration.camera_rotation[1][0];
		s.v[2] = (float)calibration.camera_rotation[2][0];

		s.v[3] = (float)calibration.camera_rotation[0][1];
		s.v[4] = (float)calibration.camera_rotation[1][1];
		s.v[5] = (float)calibration.camera_rotation[2][1];

		s.v[6] = (float)calibration.camera_rotation[0][2];
		s.v[7] = (float)calibration.camera_rotation[1][2];
		s.v[8] = (float)calibration.camera_rotation[2][2];

		xrt_pose left_in_right;
		left_in_right.position = trans;

		math_quat_from_matrix_3x3(&s, &left_in_right.orientation);

		//! @todo what are these magic values?
		//! they're probably turning the OpenCV formalism into OpenXR, but especially what gives with negating
		//! orientation.x?
		left_in_right.orientation.x = -left_in_right.orientation.x;
		left_in_right.position.y = -left_in_right.position.y;
		left_in_right.position.z = -left_in_right.position.z;

		hgt->left_in_right = left_in_right;

		HG_DEBUG(hgt, "left_in_right pose: %f %f %f   %f %f %f %f",                            //
		         left_in_right.position.x, left_in_right.position.y, left_in_right.position.z, //
		         left_in_right.orientation.x, left_in_right.orientation.y, left_in_right.orientation.z,
		         left_in_right.orientation.w);
	}

	for (int view_idx = 0; view_idx < 2; view_idx++) {
		ht_view &view = hgt->views[view_idx];
		t_camera_model_params_from_t_camera_calibration(&calibration.view[view_idx], &view.hgdist_orig);

		view.hgdist = view.hgdist_orig;
	}

	//!@todo Really? We can totally support cameras with varying resolutions.
	// For a later MR.
	hgt->calibration_one_view_size_px.w = calibration.view[0].image_size_pixels.w;
	hgt->calibration_one_view_size_px.h = calibration.view[0].image_size_pixels.h;

	hgt->last_frame_one_view_size_px = hgt->calibration_one_view_size_px;
	hgt->multiply_px_coord_for_undistort = 1.0f;

	hgt->hand_pose_camera_offset = XRT_POSE_IDENTITY;
	return true;
}

static inline bool
check_outside_view(struct HandTracking *hgt, struct t_camera_extra_info_one_view boundary, xrt_vec2 &keypoint)
{
	// Regular case - the keypoint is literally outside the image
	if (keypoint.y > hgt->last_frame_one_view_size_px.h || //
	    keypoint.y < 0 ||                                  //
	    keypoint.x > hgt->last_frame_one_view_size_px.w || //
	    keypoint.x < 0) {
		return true;
	}

	switch (boundary.boundary_type) {
	// No boundary, and we passed the previous check. Not outside the view.
	case HT_IMAGE_BOUNDARY_NONE: return false; break;
	case HT_IMAGE_BOUNDARY_CIRCLE: {
		//!@todo Optimize:  Most of this can be calculated once at startup
		xrt_vec2 center_px = {
		    boundary.boundary.circle.normalized_center.x * (float)hgt->last_frame_one_view_size_px.w, //
		    boundary.boundary.circle.normalized_center.y * (float)hgt->last_frame_one_view_size_px.h};
		float radius_px =
		    boundary.boundary.circle.normalized_radius * (float)hgt->last_frame_one_view_size_px.w;

		xrt_vec2 keypoint_xrt = {float(keypoint.x), float(keypoint.y)};

		xrt_vec2 diff = center_px - keypoint_xrt;
		if (m_vec2_len(diff) > radius_px) {
			return true;
		}
	} break;
	}

	return false;
}

static void
back_project(struct HandTracking *hgt,        //
             Eigen::Array<float, 3, 21> &pts, //
             int hand_idx,                    //
             bool also_debug_output,          //
             int num_outside[2])
{

	for (int view_idx = 0; view_idx < 2; view_idx++) {
		cv::Mat debug = hgt->views[view_idx].debug_out_to_this;
		xrt_pose move_amount = {};

		if (view_idx == 0) {
			// left camera.
			move_amount = XRT_POSE_IDENTITY;
		} else {
			move_amount = hgt->left_in_right;
		}

		Eigen::Vector3f p = map_vec3(move_amount.position);
		Eigen::Quaternionf q = map_quat(move_amount.orientation);

		Eigen::Array<float, 3, 21> pts_relative_to_camera = {};

		bool invalid[21] = {};

		for (int i = 0; i < 21; i++) {
			pts_relative_to_camera.col(i) = (q * pts.col(i)) + p;

			if (pts_relative_to_camera.col(i).z() > 0) {
				invalid[i] = true;
			}
		}

		xrt_vec2 keypoints_global[21];

		for (int i = 0; i < 21; i++) {
			invalid[i] =
			    invalid[i] || !t_camera_models_flip_and_project(&hgt->views[view_idx].hgdist,      //
			                                                    pts_relative_to_camera.col(i).x(), //
			                                                    pts_relative_to_camera.col(i).y(), //
			                                                    pts_relative_to_camera.col(i).z(), //
			                                                    &keypoints_global[i].x,            //
			                                                    &keypoints_global[i].y             //
			                  );
		}

		for (int i = 0; i < 21; i++) {
			invalid[i] = invalid[i] ||
			             check_outside_view(hgt, hgt->views[view_idx].camera_info, keypoints_global[i]);
		}

		if (num_outside != NULL) {
			num_outside[view_idx] = 0;
			for (int i = 0; i < 21; i++) {
				if (invalid[i]) {
					num_outside[view_idx]++;
				}
			}

			xrt_vec2 min = keypoints_global[0];
			xrt_vec2 max = keypoints_global[0];

			for (int i = 0; i < 21; i++) {
				xrt_vec2 &pt = keypoints_global[i];
				min.x = fmin(pt.x, min.x);
				min.y = fmin(pt.y, min.y);

				max.x = fmax(pt.x, max.x);
				max.y = fmax(pt.y, max.y);
			}
			xrt_vec2 center = m_vec2_mul_scalar(min + max, 0.5);

			float r = fmax(center.x - min.x, center.y - min.y);

			float size = r * 2;


			hgt->views[view_idx].regions_of_interest_this_frame[hand_idx].center_px = center;
			hgt->views[view_idx].regions_of_interest_this_frame[hand_idx].size_px = size;
			if (also_debug_output) {
				handSquare(debug, center, size, GREEN);
			}
		}



		if (also_debug_output) {
			for (int i = 0; i < 21; i++) {
				xrt_vec2 loc;
				loc.x = keypoints_global[i].x;
				loc.y = keypoints_global[i].y;
				handDot(debug, loc, 2, invalid[i] ? 0.0 : (float)(i) / 26.0, 1, 2);
			}
		}
	} // for view_idx
}

static void
back_project_keypoint_output(struct HandTracking *hgt, //
                             int hand_idx,             //
                             int view_idx)
{

	cv::Mat debug = hgt->views[view_idx].debug_out_to_this;
	one_frame_one_view &view = hgt->keypoint_outputs[hand_idx].views[view_idx];

	for (int i = 0; i < 21; i++) {

		//!@todo We're trivially rewriting the stereographic projection for like the 2nd or 3rd time here. We
		//! should add an Eigen template for this instead.

		xrt_vec2 dir_sg =
		    m_vec2_mul_scalar(view.keypoints_in_scaled_stereographic[i].pos_2d, view.stereographic_radius);

		float denom = (1 + dir_sg.x * dir_sg.x + dir_sg.y * dir_sg.y);
		xrt_vec3 dir = {};
		dir.x = 2 * dir_sg.x / denom;
		dir.y = 2 * dir_sg.y / denom;
		dir.z = (-1 + (dir_sg.x * dir_sg.x) + (dir_sg.y * dir_sg.y)) / denom;

		math_quat_rotate_vec3(&view.look_dir, &dir, &dir);

		xrt_vec2 loc = {};
		t_camera_models_flip_and_project(&hgt->views[view_idx].hgdist, //
		                                 dir.x,                        //
		                                 dir.y,                        //
		                                 dir.z,                        //
		                                 &loc.x,                       //
		                                 &loc.y                        //
		);

		handDot(debug, loc, 2, (float)(i) / 26.0, 1, 2);
	}
}

static bool
handle_changed_image_size(HandTracking *hgt, xrt_size &new_one_view_size)
{
	int gcd_calib = std::gcd(hgt->calibration_one_view_size_px.h, hgt->calibration_one_view_size_px.w);
	int gcd_new = std::gcd(new_one_view_size.h, new_one_view_size.w);

	int lcm_h_calib = hgt->calibration_one_view_size_px.h / gcd_calib;
	int lcm_w_calib = hgt->calibration_one_view_size_px.w / gcd_calib;

	int lcm_h_new = new_one_view_size.h / gcd_new;
	int lcm_w_new = new_one_view_size.w / gcd_new;

	bool good = (lcm_h_calib == lcm_h_new) && (lcm_w_calib == lcm_w_new);

	if (!good) {
		HG_WARN(hgt, "Can't process this frame, wrong aspect ratio. What we wanted: %dx%d, what we got: %dx%d",
		        lcm_h_calib, lcm_w_calib, lcm_h_new, lcm_w_new);
		return false;
	}

	//!@todo optimize: can't we just scale camera matrix/etc correctly?
	hgt->multiply_px_coord_for_undistort = (float)hgt->calibration_one_view_size_px.h / (float)new_one_view_size.h;
	hgt->last_frame_one_view_size_px = new_one_view_size;

	for (int view_idx = 0; view_idx < 2; view_idx++) {
		hgt->views[view_idx].hgdist.fx =
		    hgt->views[view_idx].hgdist_orig.fx / hgt->multiply_px_coord_for_undistort;
		hgt->views[view_idx].hgdist.fy =
		    hgt->views[view_idx].hgdist_orig.fy / hgt->multiply_px_coord_for_undistort;

		hgt->views[view_idx].hgdist.cx =
		    hgt->views[view_idx].hgdist_orig.cx / hgt->multiply_px_coord_for_undistort;
		hgt->views[view_idx].hgdist.cy =
		    hgt->views[view_idx].hgdist_orig.cy / hgt->multiply_px_coord_for_undistort;
	}
	return true;
}

float
hand_confidence_value(float reprojection_error, one_frame_input &input)
{
	float out_confidence = 0.0f;
	for (int view_idx = 0; view_idx < 2; view_idx++) {
		for (int i = 0; i < 21; i++) {
			// whatever
			out_confidence += input.views[view_idx].keypoints_in_scaled_stereographic[i].confidence_xy;
		}
	}
	out_confidence /= 42.0f; // number of hand joints
	float reproj_err_mul = 1.0f / ((reprojection_error * 10) + 1.0f);
	return out_confidence * reproj_err_mul;
}


xrt_vec3
correct_direction(xrt_vec2 in)
{
	xrt_vec3 out = {in.x, -in.y, -1};
	return m_vec3_normalize(out);
}

void
check_new_user_event(struct HandTracking *hgt)
{
	if (hgt->tuneable_values.new_user_event) {
		hgt->tuneable_values.new_user_event = false;
		hgt->hand_seen_before[0] = false;
		hgt->hand_seen_before[1] = false;
		hgt->refinement.hand_size_refinement_schedule_x = 0;
		hgt->refinement.optimizing = true;
		hgt->target_hand_size = STANDARD_HAND_SIZE;
	}
}



static float
hand_bounding_boxes_iou(const hand_region_of_interest &one, const hand_region_of_interest &two)
{
	if (!one.found || !two.found) {
		return -1;
	}
	box_iou::Box this_box(one.center_px, one.size_px);
	box_iou::Box other_box(two.center_px, two.size_px);

	return boxIOU(this_box, other_box);
}

void
dispatch_and_process_hand_detections(struct HandTracking *hgt)
{
	if (hgt->tuneable_values.always_run_detection_model) {
		// Pretend like nothing was detected last frame.
		for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
			hgt->this_frame_hand_detected[hand_idx] = false;

			hgt->history_hands[hand_idx].clear();
		}
	}

	hand_detection_run_info infos[2] = {};

	// Mega paranoia, should get optimized out.
	for (int view_idx = 0; view_idx < 2; view_idx++) {
		for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
			infos[view_idx].outputs[hand_idx].found = false;
			infos[view_idx].outputs[hand_idx].hand_detection_confidence = 0;
			infos[view_idx].outputs[hand_idx].provenance = ROIProvenance::HAND_DETECTION;
		}
	}



	infos[0].view = &hgt->views[0];
	infos[1].view = &hgt->views[1];



	bool no_hands_detected_last_frame = !(hgt->this_frame_hand_detected[0] || hgt->this_frame_hand_detected[1]);

	size_t active_camera = hgt->detection_counter++ % 2;

	int num_views = 0;

	if (hgt->tuneable_values.always_run_detection_model || hgt->refinement.optimizing ||
	    hgt->tuneable_values.detection_model_in_both_views) {
		u_worker_group_push(hgt->group, run_hand_detection, &infos[0]);
		u_worker_group_push(hgt->group, run_hand_detection, &infos[1]);
		num_views = 2;
		u_worker_group_wait_all(hgt->group);
	} else {
		run_hand_detection(&infos[active_camera]);
		num_views = 1;
	}


	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		float confidence_sum = (infos[0].outputs[hand_idx].hand_detection_confidence +
		                        infos[1].outputs[hand_idx].hand_detection_confidence) /
		                       float(num_views);
		if (confidence_sum < 0.92) {
			continue;
		}


		if (hgt->tuneable_values.always_run_detection_model || !hgt->last_frame_hand_detected[hand_idx]) {


			bool good_to_go = true;


			if (no_hands_detected_last_frame) {
				// Stop overlapping _double_ hand detections - detecting both hands in the same place.
				// This happens a lot if you put your hands together (we can't track intertwining hands
				// yet)
				for (int view_idx = 0; view_idx < 2; view_idx++) {
					float iou = hand_bounding_boxes_iou(infos[view_idx].outputs[hand_idx],
					                                    infos[view_idx].outputs[!hand_idx]);
					if (iou > hgt->tuneable_values.mpiou_double_detection.val) {
						HG_DEBUG(hgt,
						         "Rejected double detection because the iou for hand idx %d, "
						         "view idx "
						         "%d was %f",
						         hand_idx, view_idx, iou);
						good_to_go = false;
						break;
					}
				}
			} else {
				// Stop overlapping _single_ hand detections - detecting one hand where another hand is
				// already tracked. This happens a lot if you trick the hand detector into thinking your
				// left hand is a right hand.
				for (int view_idx = 0; view_idx < 2; view_idx++) {
					hand_region_of_interest &this_state = infos[view_idx].outputs[hand_idx];

					// Note that this is not just the other state
					hand_region_of_interest &other_state =
					    hgt->views[view_idx].regions_of_interest_this_frame[!hand_idx];
					float iou = hand_bounding_boxes_iou(this_state, other_state);
					if (iou > hgt->tuneable_values.mpiou_single_detection.val) {
						HG_DEBUG(hgt,
						         "Rejected single detection because the iou for hand idx %d, "
						         "view idx "
						         "%d was %f",
						         hand_idx, view_idx, iou);
						good_to_go = false;
						break;
					}
				}
			}


			if (good_to_go) {
				// Note we already initialized the previous-keypoints-input to nonexistent above this.
				hgt->views[0].regions_of_interest_this_frame[hand_idx] = infos[0].outputs[hand_idx];
				hgt->views[1].regions_of_interest_this_frame[hand_idx] = infos[1].outputs[hand_idx];
			}
		}


		hgt->this_frame_hand_detected[hand_idx] = true;
	}
}

void
hand_joint_set_to_eigen_21(const xrt_hand_joint_set &set, Eigen::Array<float, 3, 21> &out)
{
	int acc_idx = 0;

	out.col(acc_idx++) = map_vec3(set.values.hand_joint_set_default[XRT_HAND_JOINT_WRIST].relation.pose.position);
	for (int finger = 0; finger < 5; finger++) {
		for (int joint = 1; joint < 5; joint++) {
			xrt_hand_joint j = joints_5x5_to_26[finger][joint];
			const xrt_vec3 &jp = set.values.hand_joint_set_default[j].relation.pose.position;
			out.col(acc_idx++) = map_vec3(jp);
		}
	}
}

// Most of the time, this codepath runs - we predict where the hand should be based on the last
// two frames.
void
predict_new_regions_of_interest(struct HandTracking *hgt)
{

	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		// If we don't have the past two frames, this code doesn't do what we want.
		// If we only have *one* frame, we just reuse the same bounding box and hope the hand
		// hasn't moved too much. @todo

		auto &hh = hgt->history_hands[hand_idx];


		if (hh.size() < 2) {
			HG_TRACE(hgt, "continuing, size is %zu", hgt->history_hands[hand_idx].size());
			continue;
		}

		// We can only do this *after* we know we're predicting - this would otherwise overwrite the detection
		// model.
		hgt->this_frame_hand_detected[hand_idx] = hgt->last_frame_hand_detected[hand_idx];

		uint64_t time_two_frames_ago = *hgt->history_timestamps.get_at_age(1);
		uint64_t time_one_frame_ago = *hgt->history_timestamps.get_at_age(0);
		uint64_t time_now = hgt->current_frame_timestamp;



		// double dt_past = (double)() / (double)U_TIME_1S_IN_NS;
		double dt_past = time_ns_to_s(time_one_frame_ago - time_two_frames_ago);

		double dt_now = time_ns_to_s(time_now - time_one_frame_ago);


		Eigen::Array<float, 3, 21> &n_minus_two = *hh.get_at_age(1);
		Eigen::Array<float, 3, 21> &n_minus_one = *hh.get_at_age(0);


		Eigen::Array<float, 3, 21> add;

		add = n_minus_one - n_minus_two;

		add *= (dt_now * hgt->tuneable_values.amount_to_lerp_prediction.val) / dt_past;

		hgt->pose_predicted_keypoints[hand_idx] = n_minus_one + add;


		int num_outside[2];
		back_project(hgt, hgt->pose_predicted_keypoints[hand_idx], hand_idx,
		             hgt->tuneable_values.scribble_predictions_into_next_frame && hgt->debug_scribble,
		             num_outside);

		for (int view_idx = 0; view_idx < 2; view_idx++) {
			if (num_outside[view_idx] < hgt->tuneable_values.max_num_outside_view) {
				hgt->views[view_idx].regions_of_interest_this_frame[hand_idx].provenance =
				    ROIProvenance::POSE_PREDICTION;
				hgt->views[view_idx].regions_of_interest_this_frame[hand_idx].found = true;

			} else {
				hgt->views[view_idx].regions_of_interest_this_frame[hand_idx].found = false;
			}
		}
	}
}

//!@todo This looks like it sucks, but it doesn't given the current architecture.
// There are two distinct failure modes here:
// * One hand goes over the other hand, and we wish to discard the hand that is being obscured.
// * One hand "ate" the other hand: easiest way to see this is by putting your hands close together and shaking them
// around.
//
// If we were only concerned about the first one, we'd do some simple depth testing to figure out which one is
// closer to the hand and only discard the further-away hand. But the second one is such a common (and bad)
// failure mode that we really just need to stop tracking all hands if they start overlapping.

//!@todo I really want to try making a discrete optimizer that looks at recent info and decides whether to drop tracking
//! for a hand, switch its handedness or switch to some forthcoming overlapping-hands model. This would likely work by
//! pruning impossible combinations, calculating a loss for each remaining option and picking the least bad one.
void
stop_everything_if_hands_are_overlapping(struct HandTracking *hgt)
{
	bool ok = true;
	for (int view_idx = 0; view_idx < 2; view_idx++) {
		hand_region_of_interest left_box = hgt->views[view_idx].regions_of_interest_this_frame[0];
		hand_region_of_interest right_box = hgt->views[view_idx].regions_of_interest_this_frame[1];
		if (!left_box.found || !right_box.found) {
			continue;
		}
		box_iou::Box this_nbox(left_box.center_px, right_box.size_px);
		box_iou::Box other_nbox(right_box.center_px, right_box.size_px);
		float iou = box_iou::boxIOU(this_nbox, other_nbox);
		if (iou > hgt->tuneable_values.mpiou_any.val) {
			HG_DEBUG(hgt, "Stopped tracking because iou was %f in view %d", iou, view_idx);
			ok = false;
			break;
		}
	}
	if (!ok) {
		for (int view_idx = 0; view_idx < 2; view_idx++) {
			for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
				hgt->views[view_idx].regions_of_interest_this_frame[hand_idx].found = false;
			}
		}
	}
}

bool
hand_too_far(struct HandTracking *hgt, xrt_hand_joint_set &set)
{
	xrt_vec3 dp = set.values.hand_joint_set_default[XRT_HAND_JOINT_PALM].relation.pose.position;
	return (m_vec3_len(dp) > hgt->tuneable_values.max_hand_dist.val);
}

void
scribble_image_boundary(struct HandTracking *hgt)
{
	for (int view_idx = 0; view_idx < 2; view_idx++) {
		struct ht_view *view = &hgt->views[view_idx];

		cv::Mat &debug_frame = view->debug_out_to_this;
		t_camera_extra_info_one_view &info = hgt->views[view_idx].camera_info;

		if (info.boundary_type == HT_IMAGE_BOUNDARY_CIRCLE) {
			int center_x = hgt->last_frame_one_view_size_px.w * info.boundary.circle.normalized_center.x;
			int center_y = hgt->last_frame_one_view_size_px.h * info.boundary.circle.normalized_center.y;
			cv::circle(debug_frame, {center_x, center_y},
			           info.boundary.circle.normalized_radius * hgt->last_frame_one_view_size_px.w,
			           hsv2rgb(270.0, 0.5, 0.5), 2);
		}
	}
}

/*
 *
 * Member functions.
 *
 */

HandTracking::HandTracking()
{
	this->base.process = &HandTracking::cCallbackProcess;
	this->base.destroy = &HandTracking::cCallbackDestroy;
	u_sink_debug_init(&this->debug_sink_ann);
	u_sink_debug_init(&this->debug_sink_model);
}

HandTracking::~HandTracking()
{
	u_sink_debug_destroy(&this->debug_sink_ann);
	u_sink_debug_destroy(&this->debug_sink_model);

	xrt_frame_reference(&this->visualizers.old_frame, NULL);

	release_onnx_wrap(&this->views[0].keypoint[0]);
	release_onnx_wrap(&this->views[0].keypoint[1]);
	release_onnx_wrap(&this->views[0].detection);


	release_onnx_wrap(&this->views[1].keypoint[0]);
	release_onnx_wrap(&this->views[1].keypoint[1]);
	release_onnx_wrap(&this->views[1].detection);

	u_worker_group_reference(&this->group, NULL);

	t_stereo_camera_calibration_reference(&this->calib, NULL);

	lm::optimizer_destroy(&this->kinematic_hands[0]);
	lm::optimizer_destroy(&this->kinematic_hands[1]);

	u_var_remove_root((void *)&this->base);
	u_frame_times_widget_teardown(&this->ft_widget);
}

void
HandTracking::cCallbackProcess(struct t_hand_tracking_sync *ht_sync,
                               struct xrt_frame *left_frame,
                               struct xrt_frame *right_frame,
                               struct xrt_hand_joint_set *out_left_hand,
                               struct xrt_hand_joint_set *out_right_hand,
                               uint64_t *out_timestamp_ns)
{
	XRT_TRACE_MARKER();

	HandTracking *hgt = (struct HandTracking *)ht_sync;

	hgt->current_frame_timestamp = left_frame->timestamp;

	struct xrt_hand_joint_set *out_xrt_hands[2] = {out_left_hand, out_right_hand};


	/*
	 * Setup views.
	 */

	assert(left_frame->width == right_frame->width);
	assert(left_frame->height == right_frame->height);

	const int full_height = left_frame->height;
	const int full_width = left_frame->width * 2;

	if ((left_frame->width != (uint32_t)hgt->last_frame_one_view_size_px.w) ||
	    (left_frame->height != (uint32_t)hgt->last_frame_one_view_size_px.h)) {
		xrt_size new_one_view_size;
		new_one_view_size.h = left_frame->height;
		new_one_view_size.w = left_frame->width;
		// Could be an assert, should never happen after first frame.
		if (!handle_changed_image_size(hgt, new_one_view_size)) {
			return;
		}
	}

	const int view_width = hgt->last_frame_one_view_size_px.w;
	const int view_height = hgt->last_frame_one_view_size_px.h;

	const cv::Size full_size = cv::Size(full_width, full_height);
	const cv::Size view_size = cv::Size(view_width, view_height);
	const cv::Point view_offsets[2] = {cv::Point(0, 0), cv::Point(view_width, 0)};

	hgt->views[0].run_model_on_this = cv::Mat(view_size, CV_8UC1, left_frame->data, left_frame->stride);
	hgt->views[1].run_model_on_this = cv::Mat(view_size, CV_8UC1, right_frame->data, right_frame->stride);


	*out_timestamp_ns = hgt->current_frame_timestamp; // No filtering, fine to do this now. Also just a reminder
	                                                  // that this took you 2 HOURS TO DEBUG THAT ONE TIME.

	hgt->debug_scribble =
	    u_sink_debug_is_active(&hgt->debug_sink_ann) && u_sink_debug_is_active(&hgt->debug_sink_model);

	cv::Mat debug_output = {};
	xrt_frame *debug_frame = nullptr;

	// If we're outputting to a debug image, setup the image.
	if (hgt->debug_scribble) {
		u_frame_create_one_off(XRT_FORMAT_R8G8B8, full_width, full_height, &debug_frame);
		debug_frame->timestamp = hgt->current_frame_timestamp;

		debug_output = cv::Mat(full_size, CV_8UC3, debug_frame->data, debug_frame->stride);

		cv::cvtColor(hgt->views[0].run_model_on_this, debug_output(cv::Rect(view_offsets[0], view_size)),
		             cv::COLOR_GRAY2BGR);
		cv::cvtColor(hgt->views[1].run_model_on_this, debug_output(cv::Rect(view_offsets[1], view_size)),
		             cv::COLOR_GRAY2BGR);

		hgt->views[0].debug_out_to_this = debug_output(cv::Rect(view_offsets[0], view_size));
		hgt->views[1].debug_out_to_this = debug_output(cv::Rect(view_offsets[1], view_size));
		scribble_image_boundary(hgt);

		// Let's check that the collage size is actually as big as we think it is
		static_assert(1064 == (8 + ((128 + 8) * 4) + ((320 + 8)) + ((80 + 8) * 2) + 8));
		static_assert(504 == (240 + 240 + 8 + 8 + 8));
		static_assert(552 == (8 + (128 + 8) * 4));

		const int w = 1064;
		const int h = 552;

		u_frame_create_one_off(XRT_FORMAT_L8, w, h, &hgt->visualizers.xrtframe);
		hgt->visualizers.xrtframe->timestamp = hgt->current_frame_timestamp;

		cv::Size size = cv::Size(w, h);

		hgt->visualizers.mat =
		    cv::Mat(size, CV_8U, hgt->visualizers.xrtframe->data, hgt->visualizers.xrtframe->stride);

		if (hgt->visualizers.old_frame == NULL) {
			// There wasn't a previous frame so let's setup the background
			hgt->visualizers.mat = 255;
		} else {
			// They had better be the same size.
			memcpy(hgt->visualizers.xrtframe->data, hgt->visualizers.old_frame->data,
			       hgt->visualizers.old_frame->size);
			xrt_frame_reference(&hgt->visualizers.old_frame, NULL);
		}
	}

	check_new_user_event(hgt);



	// Every now and then if we're not already tracking both hands, try to detect new hands.
	bool saw_both_hands_last_frame = hgt->last_frame_hand_detected[0] && hgt->last_frame_hand_detected[1];
	if (!saw_both_hands_last_frame) {
		dispatch_and_process_hand_detections(hgt);
	}

	stop_everything_if_hands_are_overlapping(hgt);

	//!@todo does this go here?
	// If no hand regions of interest were found anywhere, there's no hand - register that in the state
	// tracker
	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		if (!(hgt->views[0].regions_of_interest_this_frame[hand_idx].found ||
		      hgt->views[1].regions_of_interest_this_frame[hand_idx].found)) {
			hgt->this_frame_hand_detected[hand_idx] = false;
		}
	}


	// Dispatch keypoint estimator neural nets
	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		for (int view_idx = 0; view_idx < 2; view_idx++) {
			if (!hgt->views[view_idx].regions_of_interest_this_frame[hand_idx].found) {
				continue;
			}

			struct keypoint_estimation_run_info &inf = hgt->views[view_idx].run_info[hand_idx];
			inf.view = &hgt->views[view_idx];
			inf.hand_idx = hand_idx;
			u_worker_group_push(hgt->group, hgt->keypoint_estimation_run_func,
			                    &hgt->views[view_idx].run_info[hand_idx]);
		}
	}
	u_worker_group_wait_all(hgt->group);

	// Spaghetti logic for optimizing hand size
	bool any_hands_are_only_visible_in_one_view = false;

	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		any_hands_are_only_visible_in_one_view =                             //
		    any_hands_are_only_visible_in_one_view ||                        //
		    (hgt->views[0].regions_of_interest_this_frame[hand_idx].found != //
		     hgt->views[1].regions_of_interest_this_frame[hand_idx].found);
	}

	constexpr float mul_max = 1.0;
	constexpr float frame_max = 100;
	bool optimize_hand_size;

	if ((hgt->refinement.hand_size_refinement_schedule_x > frame_max)) {
		hgt->refinement.hand_size_refinement_schedule_y = mul_max;
		optimize_hand_size = false;
		hgt->refinement.optimizing = false;
	} else {
		hgt->refinement.hand_size_refinement_schedule_y =
		    powf((hgt->refinement.hand_size_refinement_schedule_x / frame_max), 2) * mul_max;
		optimize_hand_size = true;
		hgt->refinement.optimizing = true;
	}

	if (any_hands_are_only_visible_in_one_view) {
		optimize_hand_size = false;
	}


	// if either hand was not visible before the last new-user event but is visible now, reset the schedule
	// a bit.
	if ((hgt->this_frame_hand_detected[0] && !hgt->hand_seen_before[0]) ||
	    (hgt->this_frame_hand_detected[1] && !hgt->hand_seen_before[1])) {
		hgt->refinement.hand_size_refinement_schedule_x =
		    std::min(hgt->refinement.hand_size_refinement_schedule_x, frame_max / 2);
	}

	optimize_hand_size = optimize_hand_size && hgt->tuneable_values.optimize_hand_size;

	int num_hands = 0;
	float avg_hand_size = 0;

	// Dispatch the optimizers!
	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {


		for (int view_idx = 0; view_idx < 2; view_idx++) {
			if (!hgt->views[view_idx].regions_of_interest_this_frame[hand_idx].found) {
				// to the next view
				continue;
			}

			if (!hgt->keypoint_outputs[hand_idx].views[view_idx].active) {
				HG_DEBUG(hgt, "Removing hand %d because keypoint estimator said to!", hand_idx);
				hgt->this_frame_hand_detected[hand_idx] = false;
			}
		}

		if (!hgt->this_frame_hand_detected[hand_idx]) {
			continue;
		}


		for (int view = 0; view < 2; view++) {
			hand_region_of_interest &from_model = hgt->views[view].regions_of_interest_this_frame[hand_idx];
			if (!from_model.found) {
				hgt->keypoint_outputs[hand_idx].views[view].active = false;
			}
		}

		if (hgt->tuneable_values.scribble_keypoint_model_outputs && hgt->debug_scribble) {
			for (int view_idx = 0; view_idx < 2; view_idx++) {

				if (!hgt->keypoint_outputs[hand_idx].views[view_idx].active) {
					continue;
				}

				back_project_keypoint_output(hgt, hand_idx, view_idx);
			}
		}

		struct xrt_hand_joint_set *put_in_set = out_xrt_hands[hand_idx];

		lm::KinematicHandLM *hand = hgt->kinematic_hands[hand_idx];

		float reprojection_error_threshold = hgt->tuneable_values.max_reprojection_error.val;
		float smoothing_factor = hgt->tuneable_values.opt_smooth_factor.val;

		if (hgt->last_frame_hand_detected[hand_idx]) {
			if (hgt->tuneable_values.enable_framerate_based_smoothing) {
				int64_t one_before = *hgt->history_timestamps.get_at_age(0);
				int64_t now = hgt->current_frame_timestamp;

				uint64_t diff = now - one_before;
				double diff_d = time_ns_to_s(diff);
				smoothing_factor = hgt->tuneable_values.opt_smooth_factor.val * (1 / 60.0f) / diff_d;
			}
		} else {
			reprojection_error_threshold = hgt->tuneable_values.max_reprojection_error.val;
		}



		float out_hand_size;

		//!@todo optimize: We can have one of these on each thread
		float reprojection_error;
		lm::optimizer_run(hand,                                     //
		                  hgt->keypoint_outputs[hand_idx],          //
		                  !hgt->last_frame_hand_detected[hand_idx], //
		                  smoothing_factor,
		                  optimize_hand_size,                              //
		                  hgt->target_hand_size,                           //
		                  hgt->refinement.hand_size_refinement_schedule_y, //
		                  hgt->tuneable_values.amt_use_depth.val,
		                  *put_in_set,   //
		                  out_hand_size, //
		                  reprojection_error);



		if (reprojection_error > reprojection_error_threshold) {
			HG_DEBUG(hgt, "Reprojection error above threshold!");
			hgt->this_frame_hand_detected[hand_idx] = false;
			continue;
		}

		if (hand_too_far(hgt, *put_in_set)) {
			HG_DEBUG(hgt, "Hand too far away");
			hgt->this_frame_hand_detected[hand_idx] = false;
			continue;
		}


		avg_hand_size += out_hand_size;
		num_hands++;

		if (!any_hands_are_only_visible_in_one_view) {
			hgt->refinement.hand_size_refinement_schedule_x +=
			    hand_confidence_value(reprojection_error, hgt->keypoint_outputs[hand_idx]);
		}

		u_hand_joints_apply_joint_width(put_in_set);



		put_in_set->hand_pose.pose = hgt->hand_pose_camera_offset;
		put_in_set->hand_pose.relation_flags = valid_flags_ht;

		Eigen::Array<float, 3, 21> asf = {};



		hand_joint_set_to_eigen_21(*put_in_set, asf);

		back_project(hgt,                                                                    //
		             asf,                                                                    //
		             hand_idx,                                                               //
		             hgt->tuneable_values.scribble_optimizer_outputs && hgt->debug_scribble, //
		             NULL                                                                    //
		);

		hgt->history_hands[hand_idx].push_back(asf);
		hgt->hand_tracked_for_num_frames[hand_idx]++;
	}

	// Push our timestamp back as well
	hgt->history_timestamps.push_back(hgt->current_frame_timestamp);

	// More hand-size-optimization spaghetti
	if (num_hands > 0) {
		hgt->target_hand_size = (float)avg_hand_size / (float)num_hands;
	}

	// State tracker tweaks
	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		out_xrt_hands[hand_idx]->is_active = hgt->this_frame_hand_detected[hand_idx];
		hgt->last_frame_hand_detected[hand_idx] = hgt->this_frame_hand_detected[hand_idx];

		hgt->hand_seen_before[hand_idx] =
		    hgt->hand_seen_before[hand_idx] || hgt->this_frame_hand_detected[hand_idx];

		if (!hgt->last_frame_hand_detected[hand_idx]) {
			hgt->views[0].regions_of_interest_this_frame[hand_idx].found = false;
			hgt->views[1].regions_of_interest_this_frame[hand_idx].found = false;
			hgt->history_hands[hand_idx].clear();
			hgt->hand_tracked_for_num_frames[hand_idx] = 0;
		}
	}

	// estimators next frame. Also, if next frame's hand will be outside of the camera's field of view, mark it as
	// inactive this frame. This stops issues where our hand detector detects hands that are slightly too close to
	// the edge, causing flickery hands.
	if (!hgt->tuneable_values.always_run_detection_model) {
		predict_new_regions_of_interest(hgt);
		bool still_found[2] = {hgt->last_frame_hand_detected[0], hgt->last_frame_hand_detected[1]};
		still_found[0] = hgt->views[0].regions_of_interest_this_frame[0].found ||
		                 hgt->views[1].regions_of_interest_this_frame[0].found;
		still_found[1] = hgt->views[0].regions_of_interest_this_frame[1].found ||
		                 hgt->views[1].regions_of_interest_this_frame[1].found;

		for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
			out_xrt_hands[hand_idx]->is_active = still_found[hand_idx];
		}
	}

	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		// Don't send the hand to OpenXR until it's been tracked for 4 frames
		if (hgt->hand_tracked_for_num_frames[hand_idx] < hgt->tuneable_values.num_frames_before_display) {
			out_xrt_hands[hand_idx]->is_active = false;
		}
	}

	// If the debug UI is active, push to the frame-timing widget
	u_frame_times_widget_push_sample(&hgt->ft_widget, hgt->current_frame_timestamp);

	// If the debug UI is active, push our debug frame
	if (hgt->debug_scribble) {
		u_sink_debug_push_frame(&hgt->debug_sink_ann, debug_frame);
		xrt_frame_reference(&debug_frame, NULL);

		// We don't dereference the model inputs/outputs frame here; we make a copy of it next frame and
		// dereference it then.
		u_sink_debug_push_frame(&hgt->debug_sink_model, hgt->visualizers.xrtframe);
		xrt_frame_reference(&hgt->visualizers.old_frame, hgt->visualizers.xrtframe);
		xrt_frame_reference(&hgt->visualizers.xrtframe, NULL);
	}

	// done!
}

void
HandTracking::cCallbackDestroy(t_hand_tracking_sync *ht_sync)
{
	HandTracking *ht_ptr = &HandTracking::fromC(ht_sync);

	delete ht_ptr;
}

} // namespace xrt::tracking::hand::mercury


using namespace xrt::tracking::hand::mercury;

/*
 *
 * 'Exported' functions.
 *
 */

extern "C" t_hand_tracking_sync *
t_hand_tracking_sync_mercury_create(struct t_stereo_camera_calibration *calib,
                                    struct t_camera_extra_info extra_camera_info,
                                    const char *models_folder)
{
	XRT_TRACE_MARKER();

	xrt::tracking::hand::mercury::HandTracking *hgt = new xrt::tracking::hand::mercury::HandTracking();

	// Setup logging first. We like logging.
	hgt->log_level = xrt::tracking::hand::mercury::debug_get_log_option_mercury_log();

	/*
	 * Get configuration
	 */

	assert(calib != NULL);
	hgt->calib = NULL;
	// We have to reference it, getCalibration points at it.
	t_stereo_camera_calibration_reference(&hgt->calib, calib);
	getCalibration(hgt, *calib);
	strncpy(hgt->models_folder, models_folder, ARRAY_SIZE(hgt->models_folder));


	hgt->views[0].hgt = hgt;
	hgt->views[1].hgt = hgt; // :)

	hgt->views[0].camera_info = extra_camera_info.views[0];
	hgt->views[1].camera_info = extra_camera_info.views[1];

	init_hand_detection(hgt, &hgt->views[0].detection);
	init_hand_detection(hgt, &hgt->views[1].detection);

	init_keypoint_estimation(hgt, &hgt->views[0].keypoint[0]);
	init_keypoint_estimation(hgt, &hgt->views[0].keypoint[1]);

	init_keypoint_estimation(hgt, &hgt->views[1].keypoint[0]);
	init_keypoint_estimation(hgt, &hgt->views[1].keypoint[1]);
	hgt->keypoint_estimation_run_func = xrt::tracking::hand::mercury::run_keypoint_estimation;

	hgt->views[0].view = 0;
	hgt->views[1].view = 1;

	int num_threads = 4;
	hgt->pool = u_worker_thread_pool_create(num_threads - 1, num_threads, "Hand Tracking");
	hgt->group = u_worker_group_create(hgt->pool);

	lm::optimizer_create(hgt->left_in_right, false, hgt->log_level, &hgt->kinematic_hands[0]);
	lm::optimizer_create(hgt->left_in_right, true, hgt->log_level, &hgt->kinematic_hands[1]);

	u_frame_times_widget_init(&hgt->ft_widget, 10.0f, 10.0f);

	u_var_add_root(hgt, "Camera-based Hand Tracker", true);


	u_var_add_ro_f32(hgt, &hgt->ft_widget.fps, "FPS!");
	u_var_add_f32_timing(hgt, hgt->ft_widget.debug_var, "Frame timing!");

	u_var_add_f32(hgt, &hgt->target_hand_size, "Hand size (Meters between wrist and middle-proximal joint)");
	u_var_add_ro_f32(hgt, &hgt->refinement.hand_size_refinement_schedule_x, "Schedule (X value)");
	u_var_add_ro_f32(hgt, &hgt->refinement.hand_size_refinement_schedule_y, "Schedule (Y value)");


	u_var_add_bool(hgt, &hgt->tuneable_values.new_user_event, "Trigger new-user event!");

	hgt->tuneable_values.optimize_hand_size = debug_get_bool_option_mercury_optimize_hand_size();

	hgt->tuneable_values.dyn_radii_fac.max = 4.0f;
	hgt->tuneable_values.dyn_radii_fac.min = 0.3f;
	hgt->tuneable_values.dyn_radii_fac.step = 0.02f;
	hgt->tuneable_values.dyn_radii_fac.val = 1.7f;


	hgt->tuneable_values.after_detection_fac.max = 1.0f;
	hgt->tuneable_values.after_detection_fac.min = 0.1f;
	hgt->tuneable_values.after_detection_fac.step = 0.01f;
	// note that sqrt2/2 is what should make sense, but I tuned it down to this. Detection model needs work.
	hgt->tuneable_values.after_detection_fac.val = 0.65f; // but 0.5 is closer to what we actually want

	hgt->tuneable_values.dyn_joint_y_angle_error.max = 40.0f;
	hgt->tuneable_values.dyn_joint_y_angle_error.min = 0.0f;
	hgt->tuneable_values.dyn_joint_y_angle_error.step = 0.1f;
	hgt->tuneable_values.dyn_joint_y_angle_error.val = 10.0f;

	// Number of times this has been changed without rigorously testing: 1
	hgt->tuneable_values.amount_to_lerp_prediction.max = 1.5f;
	hgt->tuneable_values.amount_to_lerp_prediction.min = -1.5f;
	hgt->tuneable_values.amount_to_lerp_prediction.step = 0.01f;
	hgt->tuneable_values.amount_to_lerp_prediction.val = 0.4f;

	hgt->tuneable_values.amt_use_depth.max = 1.0f;
	hgt->tuneable_values.amt_use_depth.min = 0.0f;
	hgt->tuneable_values.amt_use_depth.step = 0.01f;
	hgt->tuneable_values.amt_use_depth.val = 0.01f;

	hgt->tuneable_values.mpiou_any.max = 1.0f;
	hgt->tuneable_values.mpiou_any.min = 0.0f;
	hgt->tuneable_values.mpiou_any.step = 0.01f;
	hgt->tuneable_values.mpiou_any.val = 0.7f;

	hgt->tuneable_values.mpiou_double_detection.max = 1.0f;
	hgt->tuneable_values.mpiou_double_detection.min = 0.0f;
	hgt->tuneable_values.mpiou_double_detection.step = 0.01f;
	hgt->tuneable_values.mpiou_double_detection.val = 0.4f;

	hgt->tuneable_values.mpiou_single_detection.max = 1.0f;
	hgt->tuneable_values.mpiou_single_detection.min = 0.0f;
	hgt->tuneable_values.mpiou_single_detection.step = 0.01f;
	hgt->tuneable_values.mpiou_single_detection.val = 0.2f;

	hgt->tuneable_values.max_reprojection_error.max = 600.0f;
	hgt->tuneable_values.max_reprojection_error.min = 0.0f;
	hgt->tuneable_values.max_reprojection_error.step = 0.001f;
	hgt->tuneable_values.max_reprojection_error.val = 15.0f;

	hgt->tuneable_values.opt_smooth_factor.max = 30.0f;
	hgt->tuneable_values.opt_smooth_factor.min = 0.0f;
	hgt->tuneable_values.opt_smooth_factor.step = 0.01f;
	hgt->tuneable_values.opt_smooth_factor.val = 2.0f;

	hgt->tuneable_values.max_hand_dist.max = 1000000.0f;
	hgt->tuneable_values.max_hand_dist.min = 0.0f;
	hgt->tuneable_values.max_hand_dist.step = 0.05f;
	hgt->tuneable_values.max_hand_dist.val = 1.7f;

	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.amt_use_depth, "Amount to use depth prediction");


	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.dyn_radii_fac, "radius factor (predict)");
	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.after_detection_fac, "radius factor (after hand detection)");
	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.dyn_joint_y_angle_error, "max error hand joint");
	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.amount_to_lerp_prediction, "Amount to lerp pose-prediction");
	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.mpiou_any, "Max permissible IOU (Any)");
	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.mpiou_double_detection,
	                        "Max permissible IOU (For suppressing double detections)");
	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.mpiou_single_detection,
	                        "Max permissible IOU (For suppressing single detections)");
	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.max_reprojection_error, "Max reprojection error");
	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.opt_smooth_factor, "Optimizer smoothing factor");
	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.max_hand_dist, "Max hand distance");

	u_var_add_i32(hgt, &hgt->tuneable_values.max_num_outside_view,
	              "max allowed number of hand joints outside view");
	u_var_add_u64(hgt, &hgt->tuneable_values.num_frames_before_display,
	              "Number of frames before we show hands to OpenXR");


	u_var_add_bool(hgt, &hgt->tuneable_values.scribble_predictions_into_next_frame,
	               "Scribble pose-predictions into next frame");
	u_var_add_bool(hgt, &hgt->tuneable_values.scribble_keypoint_model_outputs, "Scribble keypoint model output");
	u_var_add_bool(hgt, &hgt->tuneable_values.scribble_optimizer_outputs, "Scribble kinematic optimizer output");
	u_var_add_bool(hgt, &hgt->tuneable_values.always_run_detection_model,
	               "Use detection model instead of pose-predicting into next frame");
	u_var_add_bool(hgt, &hgt->tuneable_values.optimize_hand_size, "Optimize hand size");
	u_var_add_bool(hgt, &hgt->tuneable_values.enable_pose_predicted_input,
	               "Enable pose-predicted input to keypoint model");
	u_var_add_bool(hgt, &hgt->tuneable_values.enable_framerate_based_smoothing,
	               "Enable framerate-based smoothing (Don't use; surprisingly seems to make things worse)");
	u_var_add_bool(hgt, &hgt->tuneable_values.detection_model_in_both_views, "Run detection model in both views ");



	u_var_add_sink_debug(hgt, &hgt->debug_sink_ann, "Annotated camera feeds");
	u_var_add_sink_debug(hgt, &hgt->debug_sink_model, "Model inputs and outputs");

	HG_DEBUG(hgt, "Hand Tracker initialized!");

	return &hgt->base;
}
