// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking mainloop algorithm.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

#include "ht_driver.hpp"

#include "util/u_frame.h"

#include "ht_image_math.hpp"
#include "ht_models.hpp"
#include "ht_hand_math.hpp"
#include "templates/NaivePermutationSort.hpp"
#include <opencv2/imgproc.hpp>


// Flags to tell state tracker that these are indeed valid joints
static enum xrt_space_relation_flags valid_flags_ht = (enum xrt_space_relation_flags)(
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);


static void
htProcessJoint(struct ht_device *htd,
               struct xrt_vec3 model_out,
               struct xrt_hand_joint_set *hand,
               enum xrt_hand_joint idx)
{
	hand->values.hand_joint_set_default[idx].relation.relation_flags = valid_flags_ht;
	hand->values.hand_joint_set_default[idx].relation.pose.position.x = model_out.x;
	hand->values.hand_joint_set_default[idx].relation.pose.position.y = model_out.y;
	hand->values.hand_joint_set_default[idx].relation.pose.position.z = model_out.z;
}

static float
errHistory2D(HandHistory2DBBox *past, Palm7KP *present)
{
	return (m_vec2_len(*past->wrist_unfiltered[0] - present->kps[WRIST_7KP]) +
	        m_vec2_len(*past->middle_unfiltered[0] - present->kps[MIDDLE_7KP]));
}

static std::vector<Hand2D>
htImageToKeypoints(struct ht_view *htv)
{
	int view = htv->view;
	struct ht_device *htd = htv->htd;


	cv::Mat raw_input = htv->run_model_on_this;

	// Get a list of palms - drop confidences and ssd bounding boxes, just keypoints.

	std::vector<Palm7KP> hand_detections = htv->run_detection_model(htv, raw_input);

	std::vector<bool> used_histories;
	std::vector<bool> used_detections;

	std::vector<size_t> history_indices;
	std::vector<size_t> detection_indices;
	std::vector<float> dontuse;


	// Strategy here is: We have a big list of palms. Match 'em up to previous palms.
	naive_sort_permutation_by_error<HandHistory2DBBox, Palm7KP>(htv->bbox_histories, hand_detections,

	                                                            // bools
	                                                            used_histories, used_detections,

	                                                            history_indices, detection_indices, dontuse,
	                                                            errHistory2D);

	// Here's the trick - we use the associated bbox_filter to get an output but *never commit* the noisy 128x128
	// detection; instead later on we commit the (hopefully) nicer palm and wrist from the 224x224 keypoint
	// estimation.

	// Add extra detections!
	for (size_t i = 0; i < used_detections.size(); i++) {
		if (used_detections[i] == false) {
			HandHistory2DBBox hist_new = {};
			m_filter_euro_vec2_init(&hist_new.m_filter_middle, FCMIN_BBOX, FCMIN_D_BB0X, BETA_BB0X);
			m_filter_euro_vec2_init(&hist_new.m_filter_wrist, FCMIN_BBOX, FCMIN_D_BB0X, BETA_BB0X);

			// this leaks, on august 24
			htv->bbox_histories.push_back(hist_new);
			history_indices.push_back(htv->bbox_histories.size() - 1);
			detection_indices.push_back(i);
		}
	}

	// Do the things for each active bbox history!
	for (size_t i = 0; i < history_indices.size(); i++) {
		HandHistory2DBBox *hist_of_interest = &htv->bbox_histories[history_indices[i]];
		hist_of_interest->wrist_unfiltered.push(hand_detections[detection_indices[i]].kps[WRIST_7KP]);
		hist_of_interest->middle_unfiltered.push(hand_detections[detection_indices[i]].kps[MIDDLE_7KP]);
		// Eh do the rest later
	}

	// Prune stale detections! (After we don't need {history,detection}_indices to be correct)
	int bob = 0;
	for (size_t i = 0; i < used_histories.size(); i++) {
		if (used_histories[i] == false) {
			// history never got assigned a present hand to it. treat it as stale delete it.

			HT_TRACE(htv->htd, "Removing bbox from history!\n");
			htv->bbox_histories.erase(htv->bbox_histories.begin() + i + bob);
			bob--;
		}
	}
	if (htv->bbox_histories.size() == 0) {
		return {}; // bail early
	}



	std::vector<Hand2D> list_of_hands_in_bbox(
	    htv->bbox_histories.size()); // all of these are same size as htv->bbox_histories

	std::vector<std::future<Hand2D>> await_list_of_hand_in_bbox; //(htv->bbox_histories.size());

	std::vector<DetectionModelOutput> blah(htv->bbox_histories.size());

	std::vector<Hand2D> output;

	if (htv->bbox_histories.size() > 2) {
		HT_DEBUG(htd, "More than two hands (%zu) in 2D view %i", htv->bbox_histories.size(), htv->view);
	}


	for (size_t i = 0; i < htv->bbox_histories.size(); i++) { //(BBoxHistory * entry : htv->bbox_histories) {
		HandHistory2DBBox *entry = &htv->bbox_histories[i];
		cv::Mat hand_rect = cv::Mat(224, 224, CV_8UC3);
		xrt_vec2 goodenough_middle;
		xrt_vec2 goodenough_wrist;

		m_filter_euro_vec2_run_no_commit(&entry->m_filter_middle, htv->htd->current_frame_timestamp,
		                                 entry->middle_unfiltered[0], &goodenough_middle);
		m_filter_euro_vec2_run_no_commit(&entry->m_filter_wrist, htv->htd->current_frame_timestamp,
		                                 entry->wrist_unfiltered[0], &goodenough_wrist);

		rotatedRectFromJoints(htv, goodenough_middle, goodenough_wrist, &blah[i]);



		warpAffine(raw_input, hand_rect, blah[i].warp_there, hand_rect.size());

		await_list_of_hand_in_bbox.push_back(
		    std::async(std::launch::async, htd->views[view].run_keypoint_model, &htd->views[view], hand_rect));
	}

	// cut here

	for (size_t i = 0; i < htv->bbox_histories.size(); i++) {

		Hand2D in_bbox = await_list_of_hand_in_bbox[i].get();

		cv::Matx23f warp_back = blah[i].warp_back;

		Hand2D in_image_ray_coords;
		Hand2D in_image_px_coords;

		for (int i = 0; i < 21; i++) {
			struct xrt_vec3 vec = in_bbox.kps[i];

#if 1
			xrt_vec3 rr = transformVecBy2x3(vec, warp_back);
			rr.z = vec.z;
#else
			xrt_vec3 rr;
			rr.x = (vec.x * warp_back(0, 0)) + (vec.y * warp_back(0, 1)) + warp_back(0, 2);
			rr.y = (vec.x * warp_back(1, 0)) + (vec.y * warp_back(1, 1)) + warp_back(1, 2);
			rr.z = vec.z;
#endif
			in_image_px_coords.kps[i] = rr;

			in_image_ray_coords.kps[i] = raycoord(htv, rr);
			if (htd->debug_scribble) {
				handDot(htv->debug_out_to_this, {rr.x, rr.y}, fmax((-vec.z + 100 - 20) * .08, 2),
				        ((float)i) / 21.0f, cv::FILLED);
			}
		}
		xrt_vec2 middle_in_px_coords = {in_image_px_coords.kps[MIDL_PXM].x, in_image_px_coords.kps[MIDL_PXM].y};
		xrt_vec2 wrist_in_px_coords = {in_image_px_coords.kps[WRIST].x, in_image_px_coords.kps[WRIST].y};
		xrt_vec2 dontuse;
		m_filter_euro_vec2_run(&htv->bbox_histories[i].m_filter_wrist, htv->htd->current_frame_timestamp,
		                       &wrist_in_px_coords, &dontuse);

		m_filter_euro_vec2_run(&htv->bbox_histories[i].m_filter_middle, htv->htd->current_frame_timestamp,
		                       &middle_in_px_coords, &dontuse);
		output.push_back(in_image_ray_coords);
	}
	return output;
}

#if defined(JSON_OUTPUT)

static void
jsonAddJoint(cJSON *into_this, xrt_pose loc, const char *name)
{
	cJSON *container = cJSON_CreateObject();
	cJSON *joint_loc = cJSON_CreateArray();
	cJSON_AddItemToArray(joint_loc, cJSON_CreateNumber(loc.position.x));
	cJSON_AddItemToArray(joint_loc, cJSON_CreateNumber(loc.position.y));
	cJSON_AddItemToArray(joint_loc, cJSON_CreateNumber(loc.position.z));

	cJSON_AddItemToObject(container, "position", joint_loc);

	cJSON *joint_rot = cJSON_CreateArray();


	cJSON_AddItemToArray(joint_rot, cJSON_CreateNumber(loc.orientation.x));
	cJSON_AddItemToArray(joint_rot, cJSON_CreateNumber(loc.orientation.y));
	cJSON_AddItemToArray(joint_rot, cJSON_CreateNumber(loc.orientation.z));
	cJSON_AddItemToArray(joint_rot, cJSON_CreateNumber(loc.orientation.w));

	cJSON_AddItemToObject(container, "rotation_quat_xyzw", joint_rot);

	cJSON_AddItemToObject(into_this, name, container);
}


static void
jsonAddSet(struct ht_device *htd)
{
	cJSON *two_hand_container = cJSON_CreateObject();
	static const char *keys[] = {
	    "wrist",      "palm",

	    "thumb_mcp",  "thumb_pxm",  "thumb_dst",  "thumb_tip",

	    "index_mcp",  "index_pxm",  "index_int",  "index_dst",  "index_tip",

	    "middle_mcp", "middle_pxm", "middle_int", "middle_dst", "middle_tip",

	    "ring_mcp",   "ring_pxm",   "ring_int",   "ring_dst",   "ring_tip",

	    "little_mcp", "little_pxm", "little_int", "little_dst", "little_tip",
	};
	static const char *sides_names[] = {
	    "left",
	    "right",
	};
	for (int side = 0; side < 2; side++) {
		struct xrt_hand_joint_set *set = &htd->hands_for_openxr[side];
		if (!set->is_active) {
			cJSON_AddNullToObject(two_hand_container, sides_names[side]);
		} else {
			cJSON *hand_obj = cJSON_CreateObject();
			for (int i = 0; i < 26; i++) {
				const char *key = keys[i];
				xrt_pose pose = set->values.hand_joint_set_default[i].relation.pose;
				jsonAddJoint(hand_obj, pose, key);
			}
			cJSON_AddItemToObject(two_hand_container, sides_names[side], hand_obj);
		}
	}

#if defined(JSON_OUTPUT)
	cJSON_AddItemToArray(htd->output_array, two_hand_container);
#endif
}

#endif


static void
htBailThisFrame(struct ht_device *htd)
{

	os_mutex_lock(&htd->openxr_hand_data_mediator);
	htd->hands_for_openxr[0].is_active = false;
	htd->hands_for_openxr[1].is_active = false;
#if defined(JSON_OUTPUT)
	json_add_set(htd);
#endif
	os_mutex_unlock(&htd->openxr_hand_data_mediator);
}

int64_t last_frame, this_frame;

static void
htRunAlgorithm(struct ht_device *htd)
{
	XRT_TRACE_MARKER();

	htd->current_frame_timestamp = htd->frame_for_process->timestamp;

	int64_t start, end;
	start = os_monotonic_get_ns();


	/*
	 * Setup views.
	 */

	const int full_width = htd->frame_for_process->width;
	const int full_height = htd->frame_for_process->height;
	const int view_width = htd->camera.one_view_size_px.w;
	const int view_height = htd->camera.one_view_size_px.h;

	assert(full_width == view_width * 2);
	assert(full_height == view_height);

	const cv::Size full_size = cv::Size(full_width, full_height);
	const cv::Size view_size = cv::Size(view_width, view_height);
	const cv::Point view_offsets[2] = {cv::Point(0, 0), cv::Point(view_width, 0)};

	cv::Mat full_frame(full_size, CV_8UC3, htd->frame_for_process->data, htd->frame_for_process->stride);
	htd->views[0].run_model_on_this = full_frame(cv::Rect(view_offsets[0], view_size));
	htd->views[1].run_model_on_this = full_frame(cv::Rect(view_offsets[1], view_size));

	// Check this every frame. We really, really, really don't want it to ever suddenly be null.
	htd->debug_scribble = htd->debug_sink != nullptr;

	cv::Mat debug_output = {};
	xrt_frame *debug_frame = nullptr; // only use if htd->debug_scribble

	if (htd->debug_scribble) {
		u_frame_clone(htd->frame_for_process, &debug_frame);
		debug_output = cv::Mat(full_size, CV_8UC3, debug_frame->data, debug_frame->stride);
		htd->views[0].debug_out_to_this = debug_output(cv::Rect(view_offsets[0], view_size));
		htd->views[1].debug_out_to_this = debug_output(cv::Rect(view_offsets[1], view_size));
	}


	/*
	 * Do the hand tracking!
	 */

	std::future<std::vector<Hand2D>> future_left =
	    std::async(std::launch::async, htImageToKeypoints, &htd->views[0]);
	std::future<std::vector<Hand2D>> future_right =
	    std::async(std::launch::async, htImageToKeypoints, &htd->views[1]);
	std::vector<Hand2D> hands_in_left_view = future_left.get();
	std::vector<Hand2D> hands_in_right_view = future_right.get();

	end = os_monotonic_get_ns();


	this_frame = os_monotonic_get_ns();

	double time_ms = (double)(end - start) / (double)U_TIME_1MS_IN_NS;
	double _1_time = 1 / (time_ms * 0.001);

	char t[64];
	char t2[64];
	sprintf(t, "% 8.2f ms", time_ms);
	sprintf(t2, "% 8.2f fps", _1_time);
	last_frame = this_frame;


	if (htd->debug_scribble) {
		cv::putText(debug_output, t, cv::Point(30, 60), cv::FONT_HERSHEY_SIMPLEX, 1.0f, cv::Scalar(0, 255, 0),
		            4);
		cv::putText(debug_output, t2, cv::Point(30, 100), cv::FONT_HERSHEY_SIMPLEX, 1.0f, cv::Scalar(0, 255, 0),
		            4);
	} else {
		HT_DEBUG(htd, "%s", t);
		HT_DEBUG(htd, "%s", t2);
	}


	// Convenience
	uint64_t timestamp = htd->frame_for_process->timestamp;

	if (htd->debug_scribble) {
		htd->debug_sink->push_frame(htd->debug_sink, debug_frame);
		xrt_frame_reference(&debug_frame, NULL);
	}

	// Bail early this frame if no hands were detected.
	// In the long run, this'll be a silly thing - we shouldn't always take the detection model's word for it
	// especially when part of the pipeline is an arbitrary confidence threshold.
	if (hands_in_left_view.size() == 0 || hands_in_right_view.size() == 0) {
		htBailThisFrame(htd);
		return;
	}

	// Figure out how to match hands up across views.
	// Construct a matrix, where the rows are left view hands and the cols are right view hands.
	// For each cell, compute an error that's just the difference in Y ray coordinates of all the 21 keypoints. With
	// perfect cameras + models, these differences will be zero. Anything with a high difference is not the same
	// hand observed across views. For each cell, make a datatype that is: the error, the left view hand index, the
	// right view hand index. Put these in an array, sort them by lowest error. Iterate over this sorted list (not
	// in matrix-land anymore), assigning left view hands to right view hands as you go. For any elements that are
	// trying to assign an already-assigned hand, skip them. At the end, check for any hands that went un-assigned;
	// forget about those.

	// In the future, maybe we should go forward with several hand associations if there are two that are close,
	// keep track of which associations are mutually exclusive, and drop the one that fits the kinematic model less
	// well? Or drop the one that matches with previous measurements less well? Getting raw 3D poses out of line
	// intersection is not expensive.

	// Known issue: If you put your hands at both exactly the same height it will not do the right thing. Won't fix
	// right now; need to upstream *something* first.

	std::vector<bool> left_hands_taken;
	std::vector<bool> right_hands_taken;

	std::vector<size_t> l_indices_in_order;
	std::vector<size_t> r_indices_in_order;
	std::vector<float> y_disparity_error_in_order;

	naive_sort_permutation_by_error<Hand2D, Hand2D>(
	    // Inputs
	    hands_in_left_view, hands_in_right_view,

	    // Outputs
	    left_hands_taken, right_hands_taken,

	    l_indices_in_order, r_indices_in_order, y_disparity_error_in_order, errHandDisparity);

	std::vector<Hand2D> associated_in_left;
	std::vector<Hand2D> associated_in_right;


	for (size_t i = 0; i < l_indices_in_order.size(); i++) {
		associated_in_left.push_back(hands_in_left_view[i]);
		associated_in_right.push_back(hands_in_right_view[i]);
	}


	std::vector<Hand3D> hands_unfiltered; //(associated_in_left.size());

	for (size_t hand_idx = 0; hand_idx < associated_in_left.size(); hand_idx++) {

		Hand3D cur_hand;

		for (int i = 0; i < 21; i++) {
			float t = htd->baseline /
			          (associated_in_left[hand_idx].kps[i].x - associated_in_right[hand_idx].kps[i].x);
			// float x, y;

			cur_hand.kps[i].z = -t;

			cur_hand.kps[i].x = (associated_in_left[hand_idx].kps[i].x * t); //-(htd->baseline * 0.5f);
			cur_hand.kps[i].y = -associated_in_left[hand_idx].kps[i].y * t;
			cur_hand.timestamp = timestamp;

			// soon! average with hand in right view.
			cur_hand.kps[i].x += htd->baseline + (associated_in_right[hand_idx].kps[i].x * t);
			cur_hand.kps[i].y += -associated_in_right[hand_idx].kps[i].y * t;

			cur_hand.kps[i].x *= .5;
			cur_hand.kps[i].y *= .5;
		}

		if (rejectBadHand(&cur_hand)) { // reject hands!!!
			cur_hand.y_disparity_error = y_disparity_error_in_order[hand_idx];
			hands_unfiltered.push_back(cur_hand);
		} else {
			HT_DEBUG(htd, "Rejected bad hand!"); // This probably could be a warn ...
		}
	}

	// Okay now do the exact same thing but with present and past instead of with left view and right view. Lotsa
	// code but hey this is hard stuff.


	std::vector<bool> past_hands_taken;
	std::vector<bool> present_hands_taken;

	std::vector<size_t> past_indices;
	std::vector<size_t> present_indices;
	std::vector<float> flow_errors;


	naive_sort_permutation_by_error<HandHistory3D, Hand3D>(htd->histories_3d, // past
	                                                       hands_unfiltered,  // present


	                                                       // outputs
	                                                       past_hands_taken, present_hands_taken, past_indices,
	                                                       present_indices, flow_errors, errHandHistory

	);


	for (size_t i = 0; i < past_indices.size(); i++) {
		htd->histories_3d[past_indices[i]].last_hands.push(hands_unfiltered[present_indices[i]]);
	}
	// The above may not do anything, because we'll start out with no hand histories! All the numbers of elements
	// should be zero.


	for (size_t i = 0; i < present_hands_taken.size(); i++) {
		if (present_hands_taken[i] == false) {
			// if this hand never got assigned to a history
			HandHistory3D history_new;
			handEuroFiltersInit(&history_new, FCMIN_HAND, FCMIN_D_HAND, BETA_HAND);
			history_new.last_hands.push(hands_unfiltered[i]);
			// history_new.
			htd->histories_3d.push_back(
			    history_new); // Add something to the end - don't initialize any of it.
		}
	}

	int bob = 0;
	for (size_t i = 0; i < past_hands_taken.size(); i++) {
		if (past_hands_taken[i] == false) {
			htd->histories_3d.erase(htd->histories_3d.begin() + i + bob);
			bob--;
		}
	}

	if (htd->histories_3d.size() == 0) {
		HT_DEBUG(htd, "Bailing");
		htBailThisFrame(htd);
		return;
	}

	size_t num_hands = htd->histories_3d.size();
	if (num_hands > 2) {
		HT_WARN(htd, "More than two hands observed (%zu)! Expect bugginess!",
		        num_hands); // this is quite bad, but rarely happens.
	}

	// Iterate over all hands we're keeping track of, compute their current handedness.
	for (size_t i = 0; i < htd->histories_3d.size(); i++) {
		handednessHandHistory3D(&htd->histories_3d[i]);
	}

	// Whoo! Okay, now we have some unfiltered hands in htd->histories_3d[i].last_hands[0]! Euro filter them!

	std::vector<Hand3D> filtered_hands(num_hands);

	for (size_t hand_index = 0; hand_index < num_hands; hand_index++) {
		filtered_hands[hand_index] = handEuroFiltersRun(&htd->histories_3d[hand_index]);
		applyThumbIndexDrag(&filtered_hands[hand_index]);
		filtered_hands[hand_index].handedness = htd->histories_3d[hand_index].handedness;
	}

	std::vector<size_t> xr_indices;
	std::vector<Hand3D *> hands_to_use;

	if (filtered_hands.size() == 1) {
		if (filtered_hands[0].handedness < 0) {
			// Left
			xr_indices = {0};
			hands_to_use = {&filtered_hands[0]};
		} else {
			xr_indices = {1};
			hands_to_use = {&filtered_hands[0]};
		}
	} else {
		// filtered_hands better be two for now.
		if (filtered_hands[0].handedness < filtered_hands[1].handedness) {
			xr_indices = {0, 1};
			hands_to_use = {&filtered_hands[0], &filtered_hands[1]};
		} else {
			xr_indices = {1, 0};
			hands_to_use = {&filtered_hands[0], &filtered_hands[1]};
		}
	}

	struct xrt_hand_joint_set final_hands_ordered_by_handedness[2];
	memset(&final_hands_ordered_by_handedness[0], 0, sizeof(xrt_hand_joint_set));
	memset(&final_hands_ordered_by_handedness[1], 0, sizeof(xrt_hand_joint_set));
	final_hands_ordered_by_handedness[0].is_active = false;
	final_hands_ordered_by_handedness[1].is_active = false;

	for (size_t i = 0; (i < xr_indices.size()); i++) {
		Hand3D *hand = hands_to_use[i];


		struct xrt_hand_joint_set *put_in_set = &final_hands_ordered_by_handedness[xr_indices[i]];

		xrt_vec3 wrist = hand->kps[0];

		xrt_vec3 index_prox = hand->kps[5];
		xrt_vec3 middle_prox = hand->kps[9];
		xrt_vec3 ring_prox = hand->kps[13];
		xrt_vec3 pinky_prox = hand->kps[17];

		xrt_vec3 middle_to_index = m_vec3_sub(index_prox, middle_prox);
		xrt_vec3 middle_to_ring = m_vec3_sub(ring_prox, middle_prox);
		xrt_vec3 middle_to_pinky = m_vec3_sub(pinky_prox, middle_prox);

		xrt_vec3 three_fourths_down_middle_mcp =
		    m_vec3_add(m_vec3_mul_scalar(wrist, 3.0f / 4.0f), m_vec3_mul_scalar(middle_prox, 1.0f / 4.0f));

		xrt_vec3 middle_metacarpal = three_fourths_down_middle_mcp;

		float s = 0.6f;

		xrt_vec3 index_metacarpal = middle_metacarpal + m_vec3_mul_scalar(middle_to_index, s);
		xrt_vec3 ring_metacarpal = middle_metacarpal + m_vec3_mul_scalar(middle_to_ring, s);
		xrt_vec3 pinky_metacarpal = middle_metacarpal + m_vec3_mul_scalar(middle_to_pinky, s);

		float palm_ness = 0.33;
		xrt_vec3 palm =
		    m_vec3_add(m_vec3_mul_scalar(wrist, palm_ness), m_vec3_mul_scalar(middle_prox, (1.0f - palm_ness)));



		// clang-format off

		htProcessJoint(htd,palm, put_in_set, XRT_HAND_JOINT_PALM);

		htProcessJoint(htd,hand->kps[0], put_in_set, XRT_HAND_JOINT_WRIST);
		htProcessJoint(htd,hand->kps[1], put_in_set, XRT_HAND_JOINT_THUMB_METACARPAL);
		htProcessJoint(htd,hand->kps[2], put_in_set, XRT_HAND_JOINT_THUMB_PROXIMAL);
		htProcessJoint(htd,hand->kps[3], put_in_set, XRT_HAND_JOINT_THUMB_DISTAL);
		htProcessJoint(htd,hand->kps[4], put_in_set, XRT_HAND_JOINT_THUMB_TIP);

		htProcessJoint(htd,index_metacarpal, put_in_set, XRT_HAND_JOINT_INDEX_METACARPAL);
		htProcessJoint(htd,hand->kps[5], put_in_set, XRT_HAND_JOINT_INDEX_PROXIMAL);
		htProcessJoint(htd,hand->kps[6], put_in_set, XRT_HAND_JOINT_INDEX_INTERMEDIATE);
		htProcessJoint(htd,hand->kps[7], put_in_set, XRT_HAND_JOINT_INDEX_DISTAL);
		htProcessJoint(htd,hand->kps[8], put_in_set, XRT_HAND_JOINT_INDEX_TIP);

		htProcessJoint(htd,middle_metacarpal, put_in_set, XRT_HAND_JOINT_MIDDLE_METACARPAL);
		htProcessJoint(htd,hand->kps[9], put_in_set, XRT_HAND_JOINT_MIDDLE_PROXIMAL);
		htProcessJoint(htd,hand->kps[10], put_in_set, XRT_HAND_JOINT_MIDDLE_INTERMEDIATE);
		htProcessJoint(htd,hand->kps[11], put_in_set, XRT_HAND_JOINT_MIDDLE_DISTAL);
		htProcessJoint(htd,hand->kps[12], put_in_set, XRT_HAND_JOINT_MIDDLE_TIP);

		htProcessJoint(htd,ring_metacarpal, put_in_set, XRT_HAND_JOINT_RING_METACARPAL);
		htProcessJoint(htd,hand->kps[13], put_in_set, XRT_HAND_JOINT_RING_PROXIMAL);
		htProcessJoint(htd,hand->kps[14], put_in_set, XRT_HAND_JOINT_RING_INTERMEDIATE);
		htProcessJoint(htd,hand->kps[15], put_in_set, XRT_HAND_JOINT_RING_DISTAL);
		htProcessJoint(htd,hand->kps[16], put_in_set, XRT_HAND_JOINT_RING_TIP);

		htProcessJoint(htd, pinky_metacarpal, put_in_set, XRT_HAND_JOINT_LITTLE_METACARPAL);
		htProcessJoint(htd,hand->kps[17], put_in_set, XRT_HAND_JOINT_LITTLE_PROXIMAL);
		htProcessJoint(htd,hand->kps[18], put_in_set, XRT_HAND_JOINT_LITTLE_INTERMEDIATE);
		htProcessJoint(htd,hand->kps[19], put_in_set, XRT_HAND_JOINT_LITTLE_DISTAL);
		htProcessJoint(htd,hand->kps[20], put_in_set, XRT_HAND_JOINT_LITTLE_TIP);
    put_in_set->is_active = true;
    math_pose_identity(&put_in_set->hand_pose.pose);
		put_in_set->hand_pose.relation_flags = valid_flags_ht;
		// clang-format on
		applyJointWidths(put_in_set);
		applyJointOrientations(put_in_set, xr_indices[i]);
	}


	// For some reason, final_hands_ordered_by_handedness[0] is active but the other is inactive.

	os_mutex_lock(&htd->openxr_hand_data_mediator);
	memcpy(&htd->hands_for_openxr[0], &final_hands_ordered_by_handedness[0], sizeof(struct xrt_hand_joint_set));
	memcpy(&htd->hands_for_openxr[1], &final_hands_ordered_by_handedness[1], sizeof(struct xrt_hand_joint_set));

#if defined(JSON_OUTPUT)
	json_add_set(htd);
#endif
	os_mutex_unlock(&htd->openxr_hand_data_mediator);
}
