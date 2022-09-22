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
#include "util/u_hand_tracking.h"
#include "math/m_vec2.h"
#include "util/u_misc.h"
#include "xrt/xrt_frame.h"


#include <numeric>


namespace xrt::tracking::hand::mercury {
#define DEG_TO_RAD(DEG) (DEG * M_PI / 180.)

DEBUG_GET_ONCE_LOG_OPTION(mercury_log, "MERCURY_LOG", U_LOGGING_WARN)
DEBUG_GET_ONCE_BOOL_OPTION(mercury_use_simdr_keypoint, "MERCURY_USE_SIMDR_KEYPOINT", false)

// Flags to tell state tracker that these are indeed valid joints
static const enum xrt_space_relation_flags valid_flags_ht = (enum xrt_space_relation_flags)(
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

/*!
 * Setup helper functions.
 */

static bool
getCalibration(struct HandTracking *hgt, t_stereo_camera_calibration *calibration)
{
	xrt::auxiliary::tracking::StereoCameraCalibrationWrapper wrap(calibration);
	xrt_vec3 trans = {(float)wrap.camera_translation_mat(0, 0), (float)wrap.camera_translation_mat(1, 0),
	                  (float)wrap.camera_translation_mat(2, 0)};
	hgt->baseline = m_vec3_len(trans);
	HG_DEBUG(hgt, "I think the baseline is %f meters!", hgt->baseline);
	// Note, this assumes camera 0 is the left camera and camera 1 is the right camera.
	// If you find one with the opposite arrangement, you'll need to invert hgt->baseline, and look at
	// hgJointDisparityMath

	hgt->use_fisheye = wrap.view[0].use_fisheye;

	if (hgt->use_fisheye) {
		HG_DEBUG(hgt, "I think the cameras are fisheye!");
	} else {
		HG_DEBUG(hgt, "I think the cameras are not fisheye!");
	}
	{
		// Officially, I have no idea if this is row-major or col-major. Empirically it seems to work, and that
		// is all I will say.

		// It might be that the below is *transposing* the matrix, I never remember if OpenCV's R is "left in
		// right" or "right in left"
		//!@todo
		xrt_matrix_3x3 s;
		s.v[0] = wrap.camera_rotation_mat(0, 0);
		s.v[1] = wrap.camera_rotation_mat(1, 0);
		s.v[2] = wrap.camera_rotation_mat(2, 0);

		s.v[3] = wrap.camera_rotation_mat(0, 1);
		s.v[4] = wrap.camera_rotation_mat(1, 1);
		s.v[5] = wrap.camera_rotation_mat(2, 1);

		s.v[6] = wrap.camera_rotation_mat(0, 2);
		s.v[7] = wrap.camera_rotation_mat(1, 2);
		s.v[8] = wrap.camera_rotation_mat(2, 2);

		xrt_pose left_in_right;
		left_in_right.position.x = wrap.camera_translation_mat(0);
		left_in_right.position.y = wrap.camera_translation_mat(1);
		left_in_right.position.z = wrap.camera_translation_mat(2);

		math_quat_from_matrix_3x3(&s, &left_in_right.orientation);
		left_in_right.orientation.x = -left_in_right.orientation.x;
		left_in_right.position.y = -left_in_right.position.y;
		left_in_right.position.z = -left_in_right.position.z;

		hgt->left_in_right = left_in_right;

		HG_DEBUG(hgt, "left_in_right pose: %f %f %f   %f %f %f %f",                            //
		         left_in_right.position.x, left_in_right.position.y, left_in_right.position.z, //
		         left_in_right.orientation.x, left_in_right.orientation.y, left_in_right.orientation.z,
		         left_in_right.orientation.w);
	}



	//* Good enough guess that view 0 and view 1 are the same size.
	for (int i = 0; i < 2; i++) {
		hgt->views[i].cameraMatrix = wrap.view[i].intrinsics_mat;

		if (hgt->use_fisheye) {
			hgt->views[i].distortion = wrap.view[i].distortion_fisheye_mat;
		} else {
			hgt->views[i].distortion = wrap.view[i].distortion_mat;
		}

		if (hgt->log_level <= U_LOGGING_DEBUG) {
			HG_DEBUG(hgt, "K%d ->", i);
			std::cout << hgt->views[i].cameraMatrix << std::endl;

			HG_DEBUG(hgt, "D%d ->", i);
			std::cout << hgt->views[i].distortion << std::endl;
		}
	}

	hgt->calibration_one_view_size_px.w = wrap.view[0].image_size_pixels.w;
	hgt->calibration_one_view_size_px.h = wrap.view[0].image_size_pixels.h;

	hgt->last_frame_one_view_size_px = hgt->calibration_one_view_size_px;
	hgt->multiply_px_coord_for_undistort = 1.0f;

	hgt->hand_pose_camera_offset = XRT_POSE_IDENTITY;



	return true;
}

static void
getModelsFolder(struct HandTracking *hgt)
{
// Please bikeshed me on this! I don't know where is the best place to put this stuff!
#if 0
	char exec_location[1024] = {};
	readlink("/proc/self/exe", exec_location, 1024);

	HG_DEBUG(hgt, "Exec at %s\n", exec_location);

	int end = 0;
	while (exec_location[end] != '\0') {
		HG_DEBUG(hgt, "%d", end);
		end++;
	}

	while (exec_location[end] != '/' && end != 0) {
		HG_DEBUG(hgt, "%d %c", end, exec_location[end]);
		exec_location[end] = '\0';
		end--;
	}

	strcat(exec_location, "../share/monado/hand-tracking-models/");
	strcpy(hgt->startup_config.model_slug, exec_location);
#else
	const char *xdg_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xdg_home != NULL) {
		strcpy(hgt->models_folder, xdg_home);
	} else if (home != NULL) {
		strcpy(hgt->models_folder, home);
	} else {
		assert(false);
	}
	strcat(hgt->models_folder, "/.local/share/monado/hand-tracking-models/");
#endif
}

template <typename Vec>
static inline bool
check_outside_view(struct HandTracking *hgt, struct t_camera_extra_info_one_view boundary, Vec &keypoint)
{
	// Regular case - the keypoint is literally outside the image
	if (keypoint.y > hgt->calibration_one_view_size_px.h || //
	    keypoint.y < 0 ||                                   //
	    keypoint.x > hgt->calibration_one_view_size_px.w || //
	    keypoint.x < 0) {
		return true;
	}

	switch (boundary.boundary_type) {
	// No boundary, and we passed the previous check. Not outside the view.
	case HT_IMAGE_BOUNDARY_NONE: return false; break;
	case HT_IMAGE_BOUNDARY_CIRCLE: {
		//!@optimize Most of this can be calculated once at startup
		xrt_vec2 center_px = {
		    boundary.boundary.circle.normalized_center.x * (float)hgt->calibration_one_view_size_px.w, //
		    boundary.boundary.circle.normalized_center.y * (float)hgt->calibration_one_view_size_px.h};
		float radius_px =
		    boundary.boundary.circle.normalized_radius * (float)hgt->calibration_one_view_size_px.w;

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
back_project(struct HandTracking *hgt,
             xrt_hand_joint_set *in,
             bool also_debug_output,
             xrt_vec2 centers[2],
             float radii[2],
             int num_outside[2])
{

	for (int view_idx = 0; view_idx < 2; view_idx++) {
		xrt_pose move_amount;

		if (view_idx == 0) {
			// left camera.
			move_amount = XRT_POSE_IDENTITY;
		} else {
			move_amount = hgt->left_in_right;
		}
		std::vector<cv::Point3d> pts_relative_to_camera(26);

		// Bandaid solution, doesn't quite fix things on WMR.
		bool any_joint_behind_camera = false;

		for (int i = 0; i < 26; i++) {
			xrt_vec3 tmp;
			math_quat_rotate_vec3(&move_amount.orientation,
			                      &in->values.hand_joint_set_default[i].relation.pose.position, &tmp);
			pts_relative_to_camera[i].x = tmp.x + move_amount.position.x;
			pts_relative_to_camera[i].y = tmp.y + move_amount.position.y;
			pts_relative_to_camera[i].z = tmp.z + move_amount.position.z;

			pts_relative_to_camera[i].y *= -1;
			pts_relative_to_camera[i].z *= -1;

			if (pts_relative_to_camera[i].z < 0) {
				any_joint_behind_camera = true;
			}
		}


		std::vector<cv::Point2d> out(26);
		//!@opencv_camera The OpenCV, it hurts
		if (hgt->use_fisheye) {
			cv::Affine3f aff = cv::Affine3f::Identity();
			cv::fisheye::projectPoints(pts_relative_to_camera, out, aff, hgt->views[view_idx].cameraMatrix,
			                           hgt->views[view_idx].distortion);
		} else {
			cv::Affine3d aff = cv::Affine3d::Identity();
			// cv::Matx33d rvec = cv::Matx33d::
			cv::Matx33d rotation = aff.rotation();
			cv::projectPoints(pts_relative_to_camera, rotation, aff.translation(),
			                  hgt->views[view_idx].cameraMatrix, hgt->views[view_idx].distortion, out);
		}
		xrt_vec2 keypoints_global[26];
		bool outside_view[26] = {};
		for (int i = 0; i < 26; i++) {
			if (check_outside_view(hgt, hgt->views[view_idx].camera_info, out[i]) ||
			    any_joint_behind_camera) {
				outside_view[i] = true;
				if (num_outside != NULL) {
					num_outside[view_idx]++;
				}
			}
			keypoints_global[i].x = out[i].x / hgt->multiply_px_coord_for_undistort;
			keypoints_global[i].y = out[i].y / hgt->multiply_px_coord_for_undistort;
		}

		if (centers != NULL) {
			centers[view_idx] = keypoints_global[XRT_HAND_JOINT_MIDDLE_PROXIMAL];
		}

		if (radii != NULL) {
			for (int i = 0; i < 26; i++) {
				radii[view_idx] =
				    std::max(radii[view_idx], m_vec2_len(centers[view_idx] - keypoints_global[i]));
			}
			radii[view_idx] *= hgt->tuneable_values.dyn_radii_fac.val;
		}



		cv::Mat debug = hgt->views[view_idx].debug_out_to_this;
		if (also_debug_output) {
			// for (int finger = 0; finger < 5; finger++) {
			// 	cv::Point last = {(int)keypoints_global[0].x, (int)keypoints_global[0].y};
			// 	for (int joint = 0; joint < 4; joint++) {
			// 		cv::Point the_new = {(int)keypoints_global[1 + finger * 4 + joint].x,
			// 		                     (int)keypoints_global[1 + finger * 4 + joint].y};

			// 		cv::line(debug, last, the_new, colors[0]);
			// 		last = the_new;
			// 	}
			// }

			for (int i = 0; i < 26; i++) {
				xrt_vec2 loc;
				loc.x = keypoints_global[i].x;
				loc.y = keypoints_global[i].y;
				handDot(debug, loc, 2, outside_view[i] ? 0.0 : (float)(i) / 26.0, 1, 2);
			}
		}
	} // for view_idx
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

	hgt->multiply_px_coord_for_undistort = (float)hgt->calibration_one_view_size_px.h / (float)new_one_view_size.h;
	hgt->last_frame_one_view_size_px = new_one_view_size;
	return true;
}

float
hand_confidence_value(float reprojection_error, one_frame_input &input)
{
	float out_confidence = 0.0f;
	for (int view_idx = 0; view_idx < 2; view_idx++) {
		for (int i = 0; i < 21; i++) {
			out_confidence += input.views[view_idx].confidences[i];
		}
	}
	out_confidence /= 42.0f; // number of hand joints
	float reproj_err_mul = 1.0f / (reprojection_error + 1.0f);
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
	}
}

bool
should_run_detection(struct HandTracking *hgt)
{
	if (hgt->tuneable_values.always_run_detection_model) {
		return true;
	} else {
		hgt->detection_counter++;
		// Every 30 frames, but only if we aren't tracking both hands.
		bool saw_both_hands_last_frame = hgt->last_frame_hand_detected[0] && hgt->last_frame_hand_detected[1];
		return (hgt->detection_counter % 30 == 0) && !saw_both_hands_last_frame;
	}
}

void
dispatch_and_process_hand_detections(struct HandTracking *hgt)
{
	if (hgt->tuneable_values.always_run_detection_model) {
		// Pretend like nothing was detected last frame.
		for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
			// hgt->last_frame_hand_detected[hand_idx] = false;
			hgt->this_frame_hand_detected[hand_idx] = false;

			hgt->histories[hand_idx].hands.clear();
			hgt->histories[hand_idx].timestamps.clear();
		}
	}

	// view, hand
	hand_bounding_box states[2][2] = {};

	// paranoia
	states[0]->found = false;
	states[1]->found = false;

	states[0]->confidence = 0;
	states[1]->confidence = 0;


	hand_detection_run_info infos[2] = {};

	infos[0].view = &hgt->views[0];
	infos[1].view = &hgt->views[1];

	infos[0].outputs[0] = &states[0][0];
	infos[0].outputs[1] = &states[0][1];

	infos[1].outputs[0] = &states[1][0];
	infos[1].outputs[1] = &states[1][1];


	u_worker_group_push(hgt->group, run_hand_detection, &infos[0]);
	u_worker_group_push(hgt->group, run_hand_detection, &infos[1]);
	u_worker_group_wait_all(hgt->group);
	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		if ((states[0][hand_idx].confidence + states[1][hand_idx].confidence) < 0.90) {
			continue;
		}


		//!@todo Commented out the below code, which required all detections to be pointing at roughly the same
		//! point in space.
		// We should add this back, instead using lineline.cpp. But I gotta ship this, so we're just going to be
		// less robust for now.

		// xrt_vec2 in_left = raycoord(&hgt->views[0], states[0][hand_idx].center);
		// xrt_vec2 in_right = raycoord(&hgt->views[1], states[1][hand_idx].center);

		// xrt_vec2 dir_y_l = {in_left.y, -1.0f};
		// xrt_vec2 dir_y_r = {in_right.y, -1.0f};

		// m_vec2_normalize(&dir_y_l);
		// m_vec2_normalize(&dir_y_r);

		// float minimum = cosf(DEG_TO_RAD(10));

		// float diff = m_vec2_dot(dir_y_l, dir_y_r);

		// // U_LOG_E("diff %f", diff);

		// if (diff < minimum) {
		// 	HG_DEBUG(hgt,
		// 	         "Mismatch in detection models! Diff is %f, left Y axis is %f, right Y "
		// 	         "axis is %f",
		// 	         diff, in_left.y, in_right.y);
		// 	continue;
		// }

		// If this hand was not detected last frame, we can add our prediction in.
		// Or, if we're running the model every frame.
		if (hgt->tuneable_values.always_run_detection_model || !hgt->last_frame_hand_detected[hand_idx]) {
			hgt->views[0].bboxes_this_frame[hand_idx] = states[0][hand_idx];
			hgt->views[1].bboxes_this_frame[hand_idx] = states[1][hand_idx];
		}


		hgt->this_frame_hand_detected[hand_idx] = true;
	}
	// Most of the time, this codepath runs - we predict where the hand should be based on the last
	// two frames.
}

void
predict_new_regions_of_interest(struct HandTracking *hgt)
{

	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		// If we don't have the past two frames, this code doesn't do what we want.
		// If we only have *one* frame, we just reuse the same bounding box and hope the hand
		// hasn't moved too much. @todo

		if (hgt->histories[hand_idx].timestamps.size() < 2) {
			HG_TRACE(hgt, "continuing, size is %zu", hgt->histories[hand_idx].timestamps.size());
			continue;
		}

		// We can only do this *after* we know we're predicting - this would otherwise overwrite the detection
		// model.
		hgt->this_frame_hand_detected[hand_idx] = hgt->last_frame_hand_detected[hand_idx];

		hand_history &history = hgt->histories[hand_idx];
		uint64_t time_two_frames_ago = *history.timestamps.get_at_age(1);
		uint64_t time_one_frame_ago = *history.timestamps.get_at_age(0);
		uint64_t time_now = hgt->current_frame_timestamp;

		xrt_hand_joint_set *set_two_frames_ago = history.hands.get_at_age(1);
		xrt_hand_joint_set *set_one_frame_ago = history.hands.get_at_age(0);

		// double dt_past = (double)() / (double)U_TIME_1S_IN_NS;
		double dt_past = time_ns_to_s(time_one_frame_ago - time_two_frames_ago);

		double dt_now = time_ns_to_s(time_now - time_one_frame_ago);


		xrt_vec3 vels[26];

		for (int i = 0; i < 26; i++) {


			xrt_vec3 from_to = set_one_frame_ago->values.hand_joint_set_default[i].relation.pose.position -
			                   set_two_frames_ago->values.hand_joint_set_default[i].relation.pose.position;
			vels[i] = m_vec3_mul_scalar(from_to, 1.0 / dt_past);

			// U_LOG_E("%f %f %f", vels[i].x, vels[i].y, vels[i].z);
		}
		xrt_vec3 positions_last_frame[26];
		// xrt_vec3 predicted_positions_this_frame[26];
		xrt_hand_joint_set predicted_positions_this_frame;

		for (int i = 0; i < 26; i++) {
			positions_last_frame[i] =
			    set_one_frame_ago->values.hand_joint_set_default[i].relation.pose.position;

			//!@todo I dunno if this is right.
			// Number of times this has been changed without rigorously testing: 1
			float lerp_between_last_frame_and_predicted =
			    hgt->tuneable_values.amount_to_lerp_prediction.val;
			predicted_positions_this_frame.values.hand_joint_set_default[i].relation.pose.position =
			    positions_last_frame[i] + (vels[i] * dt_now * lerp_between_last_frame_and_predicted);
		}
		xrt_vec2 centers[2] = {};
		float radii[2] = {};
		int num_outside[2] = {0, 0};

		back_project(                                                                         //
		    hgt,                                                                              //
		    &predicted_positions_this_frame,                                                  //
		    hgt->tuneable_values.scribble_predictions_into_this_frame && hgt->debug_scribble, //
		    centers,                                                                          //
		    radii,                                                                            //
		    num_outside);

		for (int view_idx = 0; view_idx < 2; view_idx++) {
			if (num_outside[view_idx] < hgt->tuneable_values.max_num_outside_view) {
				hgt->views[view_idx].bboxes_this_frame[hand_idx].center = centers[view_idx];
				hgt->views[view_idx].bboxes_this_frame[hand_idx].size_px = radii[view_idx];
				hgt->views[view_idx].bboxes_this_frame[hand_idx].found = true;
			} else {
				hgt->views[view_idx].bboxes_this_frame[hand_idx].found = false;
			}
		}
	}

	if (hgt->debug_scribble) {

		for (int view_idx = 0; view_idx < 2; view_idx++) {
			for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
				if (!hgt->views[view_idx].bboxes_this_frame[hand_idx].found) {
					continue;
				}
				xrt_vec2 &_pt = hgt->views[view_idx].bboxes_this_frame[hand_idx].center;
				float &size = hgt->views[view_idx].bboxes_this_frame[hand_idx].size_px;
				cv::Point2i pt(_pt.x, _pt.y);
				cv::rectangle(hgt->views[view_idx].debug_out_to_this,
				              cv::Rect(pt - cv::Point2i(size / 2, size / 2), cv::Size(size, size)),
				              colors[hand_idx], 1);
			}
		}
	}
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

	ccdik::free_kinematic_hand(&this->kinematic_hands_ccdik[0]);
	ccdik::free_kinematic_hand(&this->kinematic_hands_ccdik[1]);

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

		//

		struct xrt_frame *new_model_inputs_and_outputs = NULL;

		// Let's check that the collage size is actually as big as we think it is
		static_assert(1064 == (8 + ((128 + 8) * 4) + ((320 + 8)) + ((80 + 8) * 2) + 8));
		static_assert(504 == (240 + 240 + 8 + 8 + 8));

		const int w = 1064;
		const int h = 504;

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
	if (should_run_detection(hgt)) {
		dispatch_and_process_hand_detections(hgt);
	}
	// For already-tracked hands, predict where we think they should be in image space based on the past two
	// frames. Note that this always happens We want to pose-predict already tracked hands but not mess with
	// just-detected hands
	if (!hgt->tuneable_values.always_run_detection_model) {
		predict_new_regions_of_interest(hgt);
	}

	//!@todo does this go here?
	// If no hand regions of interest were found anywhere, there's no hand - register that in the state tracker
	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		if (!(hgt->views[0].bboxes_this_frame[hand_idx].found ||
		      hgt->views[1].bboxes_this_frame[hand_idx].found)) {
			hgt->this_frame_hand_detected[hand_idx] = false;
		}
	}


	// Dispatch keypoint estimator neural nets
	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		for (int view_idx = 0; view_idx < 2; view_idx++) {

			if (!hgt->views[view_idx].bboxes_this_frame[hand_idx].found) {
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
		any_hands_are_only_visible_in_one_view =                //
		    any_hands_are_only_visible_in_one_view ||           //
		    (hgt->views[0].bboxes_this_frame[hand_idx].found != //
		     hgt->views[1].bboxes_this_frame[hand_idx].found);
	}

	constexpr float mul_max = 1.0;
	constexpr float frame_max = 100;
	bool optimize_hand_size;

	if ((hgt->refinement.hand_size_refinement_schedule_x > frame_max)) {
		hgt->refinement.hand_size_refinement_schedule_y = mul_max;
		optimize_hand_size = false;
	} else {
		hgt->refinement.hand_size_refinement_schedule_y =
		    powf((hgt->refinement.hand_size_refinement_schedule_x / frame_max), 2) * mul_max;
		optimize_hand_size = true;
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

	int num_hands = 0;
	float avg_hand_size = 0;

	// Dispatch the optimizers!
	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		if (!hgt->this_frame_hand_detected[hand_idx]) {
			continue;
		}

		one_frame_input input;

		for (int view = 0; view < 2; view++) {
			keypoint_output *from_model = &hgt->views[view].keypoint_outputs[hand_idx];
			input.views[view].active = hgt->views[view].bboxes_this_frame[hand_idx].found;
			if (!input.views[view].active) {
				continue;
			}
			for (int i = 0; i < 21; i++) {
				input.views[view].confidences[i] = from_model->hand_tan_space.confidences[i];
				// std::cout << input.views[view].confidences[i] << std::endl;
				input.views[view].rays[i] = correct_direction(from_model->hand_tan_space.kps[i]);
			}
		}



		struct xrt_hand_joint_set *put_in_set = out_xrt_hands[hand_idx];

		if (__builtin_expect(!hgt->tuneable_values.use_ccdik, true)) {
			lm::KinematicHandLM *hand = hgt->kinematic_hands[hand_idx];

			//!@todo
			// ABOUT TWO MINUTES OF THOUGHT WERE PUT INTO THIS VALUE
			float reprojection_error_threshold = 0.35f;

			float out_hand_size;

			//!@optimize We can have one of these on each thread
			float reprojection_error;
			lm::optimizer_run(hand,                                            //
			                  input,                                           //
			                  !hgt->last_frame_hand_detected[hand_idx],        //
			                  optimize_hand_size,                              //
			                  hgt->target_hand_size,                           //
			                  hgt->refinement.hand_size_refinement_schedule_y, //
			                  *put_in_set,                                     //
			                  out_hand_size,                                   //
			                  reprojection_error);

			avg_hand_size += out_hand_size;
			num_hands++;

			if (reprojection_error > reprojection_error_threshold) {
				HG_DEBUG(hgt, "Reprojection error above threshold!");
				hgt->this_frame_hand_detected[hand_idx] = false;

				continue;
			}
			if (!any_hands_are_only_visible_in_one_view) {
				hgt->refinement.hand_size_refinement_schedule_x +=
				    hand_confidence_value(reprojection_error, input);
			}

		} else {
			ccdik::KinematicHandCCDIK *hand = hgt->kinematic_hands_ccdik[hand_idx];
			if (!hgt->last_frame_hand_detected[hand_idx]) {
				ccdik::init_hardcoded_statics(hand, hgt->target_hand_size);
			}
			ccdik::optimize_new_frame(hand, input, *put_in_set);
		}



		u_hand_joints_apply_joint_width(put_in_set);

		// Just debug scribbling - remove this in hard production environment
		if (hgt->tuneable_values.scribble_optimizer_outputs && hgt->debug_scribble) {
			back_project(hgt, put_in_set, true, NULL, NULL, NULL);
		}

		put_in_set->hand_pose.pose = hgt->hand_pose_camera_offset;
		put_in_set->hand_pose.relation_flags = valid_flags_ht;


		hgt->histories[hand_idx].hands.push_back(*put_in_set);
		hgt->histories[hand_idx].timestamps.push_back(hgt->current_frame_timestamp);
	}

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
			hgt->views[0].bboxes_this_frame[hand_idx].found = false;
			hgt->views[1].bboxes_this_frame[hand_idx].found = false;
			hgt->histories[hand_idx].hands.clear();
			hgt->histories[hand_idx].timestamps.clear();
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
                                    struct t_camera_extra_info extra_camera_info)
{
	XRT_TRACE_MARKER();

	auto hgt = new xrt::tracking::hand::mercury::HandTracking();

	// Setup logging first. We like logging.
	hgt->log_level = xrt::tracking::hand::mercury::debug_get_log_option_mercury_log();
	bool use_simdr = xrt::tracking::hand::mercury::debug_get_bool_option_mercury_use_simdr_keypoint();

	/*
	 * Get configuration
	 */

	assert(calib != NULL);
	hgt->calib = NULL;
	// We have to reference it, getCalibration points at it.
	t_stereo_camera_calibration_reference(&hgt->calib, calib);
	getCalibration(hgt, calib);
	getModelsFolder(hgt);

#ifdef USE_NCNN
	{
		hgt->net = ncnn_net_create();
		ncnn_option_t opt = ncnn_option_create();
		ncnn_option_set_use_vulkan_compute(opt, 1);

		ncnn_net_set_option(hgt->net, opt);
		ncnn_net_load_param(hgt->net, "/3/clones/ncnn/batch_size_2.param");
		ncnn_net_load_model(hgt->net, "/3/clones/ncnn/batch_size_2.bin");
	}

	{
		hgt->net_keypoint = ncnn_net_create();
		ncnn_option_t opt = ncnn_option_create();
		ncnn_option_set_use_vulkan_compute(opt, 1);

		ncnn_net_set_option(hgt->net_keypoint, opt);
		ncnn_net_load_param(
		    hgt->net_keypoint,
		    "/home/moses/.local/share/monado/hand-tracking-models/grayscale_keypoint_new.param");
		ncnn_net_load_model(hgt->net_keypoint,
		                    "/home/moses/.local/share/monado/hand-tracking-models/grayscale_keypoint_new.bin");
	}


#endif

	hgt->views[0].hgt = hgt;
	hgt->views[1].hgt = hgt; // :)

	hgt->views[0].camera_info = extra_camera_info.views[0];
	hgt->views[1].camera_info = extra_camera_info.views[1];

	init_hand_detection(hgt, &hgt->views[0].detection);
	init_hand_detection(hgt, &hgt->views[1].detection);

	if (use_simdr) {
		init_keypoint_estimation(hgt, &hgt->views[0].keypoint[0]);
		init_keypoint_estimation(hgt, &hgt->views[0].keypoint[1]);

		init_keypoint_estimation(hgt, &hgt->views[1].keypoint[0]);
		init_keypoint_estimation(hgt, &hgt->views[1].keypoint[1]);
		hgt->keypoint_estimation_run_func = xrt::tracking::hand::mercury::run_keypoint_estimation;
	} else {
		init_keypoint_estimation_new(hgt, &hgt->views[0].keypoint[0]);
		init_keypoint_estimation_new(hgt, &hgt->views[0].keypoint[1]);

		init_keypoint_estimation_new(hgt, &hgt->views[1].keypoint[0]);
		init_keypoint_estimation_new(hgt, &hgt->views[1].keypoint[1]);
		hgt->keypoint_estimation_run_func = xrt::tracking::hand::mercury::run_keypoint_estimation_new;
	}

	hgt->views[0].view = 0;
	hgt->views[1].view = 1;

	int num_threads = 4;
	hgt->pool = u_worker_thread_pool_create(num_threads - 1, num_threads);
	hgt->group = u_worker_group_create(hgt->pool);

	lm::optimizer_create(hgt->left_in_right, false, hgt->log_level, &hgt->kinematic_hands[0]);
	lm::optimizer_create(hgt->left_in_right, true, hgt->log_level, &hgt->kinematic_hands[1]);

	ccdik::alloc_kinematic_hand(hgt->left_in_right, false, &hgt->kinematic_hands_ccdik[0]);
	ccdik::alloc_kinematic_hand(hgt->left_in_right, true, &hgt->kinematic_hands_ccdik[1]);

	u_frame_times_widget_init(&hgt->ft_widget, 10.0f, 10.0f);

	u_var_add_root(hgt, "Camera-based Hand Tracker", true);


	u_var_add_ro_f32(hgt, &hgt->ft_widget.fps, "FPS!");
	u_var_add_f32_timing(hgt, hgt->ft_widget.debug_var, "Frame timing!");

	u_var_add_ro_f32(hgt, &hgt->target_hand_size, "Hand size (Meters between wrist and middle-proximal joint)");
	u_var_add_ro_f32(hgt, &hgt->refinement.hand_size_refinement_schedule_x, "Schedule (X value)");
	u_var_add_ro_f32(hgt, &hgt->refinement.hand_size_refinement_schedule_y, "Schedule (Y value)");


	u_var_add_bool(hgt, &hgt->tuneable_values.new_user_event, "Trigger new-user event!");

	hgt->tuneable_values.dyn_radii_fac.max = 4.0f;
	hgt->tuneable_values.dyn_radii_fac.min = 0.3f;
	hgt->tuneable_values.dyn_radii_fac.step = 0.02f;
	hgt->tuneable_values.dyn_radii_fac.val = 3.0f;

	hgt->tuneable_values.dyn_joint_y_angle_error.max = 40.0f;
	hgt->tuneable_values.dyn_joint_y_angle_error.min = 0.0f;
	hgt->tuneable_values.dyn_joint_y_angle_error.step = 0.1f;
	hgt->tuneable_values.dyn_joint_y_angle_error.val = 10.0f;

	// Number of times this has been changed without rigorously testing: 1
	hgt->tuneable_values.amount_to_lerp_prediction.max = 1.5f;
	hgt->tuneable_values.amount_to_lerp_prediction.min = -1.5f;
	hgt->tuneable_values.amount_to_lerp_prediction.step = 0.01f;
	hgt->tuneable_values.amount_to_lerp_prediction.val = 0.4f;


	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.dyn_radii_fac, "radius factor (predict)");
	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.dyn_joint_y_angle_error, "max error hand joint");
	u_var_add_draggable_f32(hgt, &hgt->tuneable_values.amount_to_lerp_prediction, "Amount to lerp pose-prediction");

	u_var_add_bool(hgt, &hgt->tuneable_values.scribble_predictions_into_this_frame, "Scribble pose-predictions");
	u_var_add_bool(hgt, &hgt->tuneable_values.scribble_keypoint_model_outputs, "Scribble keypoint model output");
	u_var_add_bool(hgt, &hgt->tuneable_values.scribble_optimizer_outputs, "Scribble kinematic optimizer output");
	u_var_add_bool(hgt, &hgt->tuneable_values.always_run_detection_model, "Always run detection model");
	u_var_add_bool(hgt, &hgt->tuneable_values.use_ccdik,
	               "Use IK optimizer (may put tracking in unexpected state, use with care)");


	u_var_add_sink_debug(hgt, &hgt->debug_sink_ann, "Annotated camera feeds");
	u_var_add_sink_debug(hgt, &hgt->debug_sink_model, "Model inputs and outputs");

	HG_DEBUG(hgt, "Hand Tracker initialized!");

	return &hgt->base;
}
