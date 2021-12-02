// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking mainloop algorithm.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#include "xrt/xrt_defines.h"

#include "math/m_vec2.h"
#include "util/u_frame.h"
#include "util/u_trace_marker.h"

#include "ht_algorithm.hpp"
#include "ht_driver.hpp"
#include "ht_hand_math.hpp"
#include "ht_image_math.hpp"
#include "ht_model.hpp"
#include "templates/NaivePermutationSort.hpp"

#include <future>

// Flags to tell state tracker that these are indeed valid joints
static const enum xrt_space_relation_flags valid_flags_ht = (enum xrt_space_relation_flags)(
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
	if (!past->htAlgorithm_approves) {
		// U_LOG_E("Returning big number because htAlgorithm told me to!");
		return 100000000000000000000000000000.0f;
	}
	float sum_of_lengths = m_vec2_len(past->wrist_unfiltered.back() - past->middle_unfiltered.back()) +
	                       m_vec2_len(present->kps[WRIST_7KP] - present->kps[MIDDLE_7KP]);

	float sum_of_distances = (m_vec2_len(past->wrist_unfiltered.back() - present->kps[WRIST_7KP]) +
	                          m_vec2_len(past->middle_unfiltered.back() - present->kps[MIDDLE_7KP]));


	float final = sum_of_distances / sum_of_lengths;

	return final;
}

static std::vector<Hand2D>
htImageToKeypoints(struct ht_view *htv)
{
	struct ht_device *htd = htv->htd;
	ht_model *htm = htv->htm;

	cv::Mat raw_input = htv->run_model_on_this;

	// Get a list of palms - drop confidences and ssd bounding boxes, just keypoints.


	std::vector<Palm7KP> hand_detections = htm->palm_detection(htv, raw_input);

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
	                                                            errHistory2D, 1.0f);

	// Here's the trick - we use the associated bbox_filter to get an output but *never commit* the noisy 128x128
	// detection; instead later on we commit the (hopefully) nicer palm and wrist from the 224x224 keypoint
	// estimation.

	// Add extra detections!
	for (size_t i = 0; i < used_detections.size(); i++) {
		if ((used_detections[i] == false) && hand_detections[i].confidence > 0.65) {
			// Confidence to get in the door is 0.65, confidence to stay in is 0.3
			HandHistory2DBBox hist_new = {};
			m_filter_euro_vec2_init(&hist_new.m_filter_center, FCMIN_BBOX_POSITION, FCMIN_D_BB0X_POSITION,
			                        BETA_BB0X_POSITION);
			m_filter_euro_vec2_init(&hist_new.m_filter_direction, FCMIN_BBOX_ORIENTATION,
			                        FCMIN_D_BB0X_ORIENTATION, BETA_BB0X_ORIENTATION);

			htv->bbox_histories.push_back(hist_new);
			history_indices.push_back(htv->bbox_histories.size() - 1);
			detection_indices.push_back(i);
		}
	}

	// Do the things for each active bbox history!
	for (size_t i = 0; i < history_indices.size(); i++) {
		HandHistory2DBBox *hist_of_interest = &htv->bbox_histories[history_indices[i]];
		hist_of_interest->wrist_unfiltered.push_back(hand_detections[detection_indices[i]].kps[WRIST_7KP]);
		hist_of_interest->index_unfiltered.push_back(hand_detections[detection_indices[i]].kps[INDEX_7KP]);
		hist_of_interest->middle_unfiltered.push_back(hand_detections[detection_indices[i]].kps[MIDDLE_7KP]);
		hist_of_interest->pinky_unfiltered.push_back(hand_detections[detection_indices[i]].kps[LITTLE_7KP]);
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

	std::vector<std::future<Hand2D>> await_list_of_hand_in_bbox; //(htv->bbox_histories.size());

	std::vector<DetectionModelOutput> blah(htv->bbox_histories.size());

	std::vector<Hand2D> output;

	if (htv->bbox_histories.size() > 2) {
		HT_DEBUG(htd, "More than two hands (%zu) in 2D view %i", htv->bbox_histories.size(), htv->view);
	}

	for (size_t i = 0; i < htv->bbox_histories.size(); i++) { //(BBoxHistory * entry : htv->bbox_histories) {
		HandHistory2DBBox *entry = &htv->bbox_histories[i];
		cv::Mat hand_rect = cv::Mat(224, 224, CV_8UC3);
		xrt_vec2 unfiltered_middle;
		xrt_vec2 unfiltered_direction;

		centerAndRotationFromJoints(htv, &entry->wrist_unfiltered.back(), &entry->index_unfiltered.back(),
		                            &entry->middle_unfiltered.back(), &entry->pinky_unfiltered.back(),
		                            &unfiltered_middle, &unfiltered_direction);

		xrt_vec2 filtered_middle;
		xrt_vec2 filtered_direction;

		m_filter_euro_vec2_run_no_commit(&entry->m_filter_center, htv->htd->current_frame_timestamp,
		                                 &unfiltered_middle, &filtered_middle);
		m_filter_euro_vec2_run_no_commit(&entry->m_filter_direction, htv->htd->current_frame_timestamp,
		                                 &unfiltered_direction, &filtered_direction);

		rotatedRectFromJoints(htv, filtered_middle, filtered_direction, &blah[i]);

		warpAffine(raw_input, hand_rect, blah[i].warp_there, hand_rect.size());

		await_list_of_hand_in_bbox.push_back(
		    std::async(std::launch::async, std::bind(&ht_model::hand_landmark, htm, hand_rect)));
	}

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
			if (htd->debug_scribble && htd->dynamic_config.scribble_2d_keypoints) {
				handDot(htv->debug_out_to_this, {rr.x, rr.y}, fmax((-vec.z + 100 - 20) * .08, 2),
				        ((float)i) / 21.0f, 0.95f, cv::FILLED);
			}
		}
		xrt_vec2 wrist_in_px_coords = {in_image_px_coords.kps[WRIST].x, in_image_px_coords.kps[WRIST].y};
		xrt_vec2 index_in_px_coords = {in_image_px_coords.kps[INDX_PXM].x, in_image_px_coords.kps[INDX_PXM].y};
		xrt_vec2 middle_in_px_coords = {in_image_px_coords.kps[MIDL_PXM].x, in_image_px_coords.kps[MIDL_PXM].y};
		xrt_vec2 little_in_px_coords = {in_image_px_coords.kps[LITL_PXM].x, in_image_px_coords.kps[LITL_PXM].y};
		xrt_vec2 dontuse;

		xrt_vec2 unfiltered_middle, unfiltered_direction;
		centerAndRotationFromJoints(htv, &wrist_in_px_coords, &index_in_px_coords, &middle_in_px_coords,
		                            &little_in_px_coords, &unfiltered_middle, &unfiltered_direction);

		m_filter_euro_vec2_run(&htv->bbox_histories[i].m_filter_center, htv->htd->current_frame_timestamp,
		                       &unfiltered_middle, &dontuse);

		m_filter_euro_vec2_run(&htv->bbox_histories[i].m_filter_direction, htv->htd->current_frame_timestamp,
		                       &unfiltered_direction, &dontuse);

		output.push_back(in_image_ray_coords);
	}
	return output;
}

#if defined(EXPERIMENTAL_DATASET_RECORDING)

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

void
jsonMaybeAddSomeHands(struct ht_device *htd, bool err)
{
	if (!htd->tracking_should_record_dataset) {
		return;
	}
	cJSON *j_this_frame = cJSON_CreateObject();
	cJSON_AddItemToObject(j_this_frame, "seq_since_start", cJSON_CreateNumber(htd->gst.current_index));
	cJSON_AddItemToObject(j_this_frame, "seq_src", cJSON_CreateNumber(htd->frame_for_process->source_sequence));
	cJSON_AddItemToObject(j_this_frame, "ts", cJSON_CreateNumber(htd->gst.last_frame_ns));

	cJSON *j_hands_in_frame = cJSON_AddArrayToObject(j_this_frame, "detected_hands");
	if (!err) {
		for (size_t idx_hand = 0; idx_hand < htd->histories_3d.size(); idx_hand++) {
			cJSON *j_hand_in_frame = cJSON_CreateObject();

			cJSON *j_uuid = cJSON_CreateNumber(htd->histories_3d[idx_hand].uuid);
			cJSON_AddItemToObject(j_hand_in_frame, "uuid", j_uuid);

			cJSON *j_handedness = cJSON_CreateNumber(htd->histories_3d[idx_hand].handedness);
			cJSON_AddItemToObject(j_hand_in_frame, "handedness", j_handedness);

			static const char *keys[21] = {
			    "WRIST",

			    "THMB_MCP", "THMB_PXM", "THMB_DST", "THMB_TIP",

			    "INDX_PXM", "INDX_INT", "INDX_DST", "INDX_TIP",

			    "MIDL_PXM", "MIDL_INT", "MIDL_DST", "MIDL_TIP",

			    "RING_PXM", "RING_INT", "RING_DST", "RING_TIP",

			    "LITL_PXM", "LITL_INT", "LITL_DST", "LITL_TIP",
			};

			for (int idx_joint = 0; idx_joint < 21; idx_joint++) {
				// const char* key = keys[idx_joint];
				cJSON *j_vec3 = cJSON_AddArrayToObject(j_hand_in_frame, keys[idx_joint]);
				cJSON_AddItemToArray(
				    j_vec3,
				    cJSON_CreateNumber(
				        htd->histories_3d[idx_hand].last_hands_unfiltered.back().kps[idx_joint].x));
				cJSON_AddItemToArray(
				    j_vec3,
				    cJSON_CreateNumber(
				        htd->histories_3d[idx_hand].last_hands_unfiltered.back().kps[idx_joint].y));
				cJSON_AddItemToArray(
				    j_vec3,
				    cJSON_CreateNumber(
				        htd->histories_3d[idx_hand].last_hands_unfiltered.back().kps[idx_joint].z));
			}


			cJSON_AddItemToArray(j_hands_in_frame, j_hand_in_frame);
		}
	}
	cJSON_AddItemToArray(htd->gst.output_array, j_this_frame);
}

#endif



static void
htExitFrame(struct ht_device *htd,
            bool err,
            struct xrt_hand_joint_set final_hands_ordered_by_handedness[2],
            uint64_t timestamp)
{

	os_mutex_lock(&htd->openxr_hand_data_mediator);
	if (err) {
		htd->hands_for_openxr[0].is_active = false;
		htd->hands_for_openxr[1].is_active = false;
	} else {
		memcpy(&htd->hands_for_openxr[0], &final_hands_ordered_by_handedness[0],
		       sizeof(struct xrt_hand_joint_set));
		memcpy(&htd->hands_for_openxr[1], &final_hands_ordered_by_handedness[1],
		       sizeof(struct xrt_hand_joint_set));
		htd->hands_for_openxr_timestamp = timestamp;
		HT_DEBUG(htd, "Adding ts %zu", htd->hands_for_openxr_timestamp);
	}
	os_mutex_unlock(&htd->openxr_hand_data_mediator);
#ifdef EXPERIMENTAL_DATASET_RECORDING
	if (htd->tracking_should_record_dataset) {
		// Add nothing-entry to json file.
		jsonMaybeAddSomeHands(htd, err);
		htd->gst.current_index++;
	}
#endif
}


static void
htJointDisparityMath(struct ht_device *htd, Hand2D *hand_in_left, Hand2D *hand_in_right, Hand3D *out_hand)
{
	for (int i = 0; i < 21; i++) {
		// Believe it or not, this is where the 3D stuff happens!
		float t = htd->baseline / (hand_in_left->kps[i].x - hand_in_right->kps[i].x);

		out_hand->kps[i].z = -t;

		out_hand->kps[i].x = (hand_in_left->kps[i].x * t);
		out_hand->kps[i].y = -hand_in_left->kps[i].y * t;

		out_hand->kps[i].x += htd->baseline + (hand_in_right->kps[i].x * t);
		out_hand->kps[i].y += -hand_in_right->kps[i].y * t;

		out_hand->kps[i].x *= .5;
		out_hand->kps[i].y *= .5;
	}
}
int64_t last_frame, this_frame;

void
htRunAlgorithm(struct ht_device *htd)
{
	XRT_TRACE_MARKER();

#ifdef EXPERIMENTAL_DATASET_RECORDING

	if (htd->tracking_should_record_dataset) {
		U_LOG_E("PUSHING!");
		uint64_t start = os_monotonic_get_ns();
		xrt_sink_push_frame(htd->gst.sink, htd->frame_for_process);
		uint64_t end = os_monotonic_get_ns();

		if ((end - start) > 0.1 * U_TIME_1MS_IN_NS) {
			U_LOG_E("Encoder overloaded!");
		}

		htd->gst.offset_ns = gstreamer_sink_get_timestamp_offset(htd->gst.gs);
		htd->gst.last_frame_ns = htd->frame_for_process->timestamp - htd->gst.offset_ns;
	}
#endif

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

	// assert(full_width == view_width * 2);
	assert(full_height == view_height);

	const cv::Size full_size = cv::Size(full_width, full_height);
	const cv::Size view_size = cv::Size(view_width, view_height);
	const cv::Point view_offsets[2] = {cv::Point(0, 0), cv::Point(view_width, 0)};

	cv::Mat full_frame(full_size, CV_8UC3, htd->frame_for_process->data, htd->frame_for_process->stride);
	htd->views[0].run_model_on_this = full_frame(cv::Rect(view_offsets[0], view_size));
	htd->views[1].run_model_on_this = full_frame(cv::Rect(view_offsets[1], view_size));

	htd->mat_for_process = &full_frame;

	// Check this every frame. We really, really, really don't want it to ever suddenly be null.
	htd->debug_scribble = htd->debug_sink.sink != nullptr;

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
		u_sink_debug_push_frame(&htd->debug_sink, debug_frame);
		xrt_frame_reference(&debug_frame, NULL);
	}

	// Bail early this frame if no hands were detected.
	// In the long run, this'll be a silly thing - we shouldn't always take the detection model's word for it
	// especially when part of the pipeline is an arbitrary confidence threshold.
	if (hands_in_left_view.size() == 0 || hands_in_right_view.size() == 0) {
		htExitFrame(htd, true, NULL, 0);
		return;
	}



	std::vector<Hand3D> possible_3d_hands;

	// for every possible combination of hands in left view and hands in right view,
	for (size_t idx_l = 0; idx_l < hands_in_left_view.size(); idx_l++) {
		for (size_t idx_r = 0; idx_r < hands_in_right_view.size(); idx_r++) {
			Hand3D cur_hand = {};

			Hand2D &left_2d = hands_in_left_view[idx_l];
			Hand2D &right_2d = hands_in_right_view[idx_r];

			// Calculate a 3D hand for this combination
			htJointDisparityMath(htd, &hands_in_left_view[idx_l], &hands_in_right_view[idx_r], &cur_hand);
			cur_hand.timestamp = timestamp;
			cur_hand.rejected_by_smush = false;

			cur_hand.idx_l = idx_l;
			cur_hand.idx_r = idx_r;

			// Calculate a y-disparity for this combination
			cur_hand.y_disparity_error = errHandDisparity(&left_2d, &right_2d);

			possible_3d_hands.push_back(cur_hand);
		}
	}

	HT_DEBUG(htd, "Starting with %zu hands!", possible_3d_hands.size());

	// For each pair of 3D hands we just made
	for (size_t idx_one = 0; idx_one < possible_3d_hands.size(); idx_one++) {
		for (size_t idx_two = 0; idx_two < possible_3d_hands.size(); idx_two++) {
			if ((idx_one <= idx_two)) {
				continue;
			}

			// See if this pair is suspiciously close together.
			// If it is, then this pairing is wrong - this is what was causing the "hands smushing together"
			// issue - we weren't catching these reliably.
			float errr = sumOfHandJointDistances(&possible_3d_hands[idx_one], &possible_3d_hands[idx_two]);
			HT_TRACE(htd, "%zu %zu is smush %f", idx_one, idx_two, errr);
			if (errr < 0.03f * 21.0f) {
				possible_3d_hands[idx_one].rejected_by_smush = true;
				possible_3d_hands[idx_two].rejected_by_smush = true;
			}
		}
	}

	std::vector<Hand3D> hands_unfiltered;

	for (Hand3D hand : possible_3d_hands) {
		// If none of these are false, then all our heuristics indicate this is a real hand, so we add it to our
		// list of real hands.
		bool selected = !hand.rejected_by_smush &&       //
		                hand.y_disparity_error < 1.0f && //
		                rejectTooClose(htd, &hand) &&    //
		                rejectTooFar(htd, &hand) &&      //
		                rejectTinyPalm(htd, &hand);
		if (selected) {
			HT_TRACE(htd, "Pushing back with y-error %f", hand.y_disparity_error);
			hands_unfiltered.push_back(hand);
		}
	}


	std::vector<bool> past_hands_taken;
	std::vector<bool> present_hands_taken;

	std::vector<size_t> past_indices;
	std::vector<size_t> present_indices;
	std::vector<float> flow_errors;


	float max_dist_between_frames = 1.0f;

	naive_sort_permutation_by_error<HandHistory3D, Hand3D>(htd->histories_3d, // past
	                                                       hands_unfiltered,  // present


	                                                       // outputs
	                                                       past_hands_taken, present_hands_taken, past_indices,
	                                                       present_indices, flow_errors, errHandHistory,
	                                                       (max_dist_between_frames * 21.0f)

	);


	for (size_t i = 0; i < past_indices.size(); i++) {
		htd->histories_3d[past_indices[i]].last_hands_unfiltered.push_back(
		    hands_unfiltered[present_indices[i]]);
	}
	// The above may not do anything, because we'll start out with no hand histories! All the numbers of elements
	// should be zero.


	for (size_t i = 0; i < present_hands_taken.size(); i++) {
		if (present_hands_taken[i] == false) {
			// if this hand never got assigned to a history
			HandHistory3D history_new;
			history_new.uuid = rand(); // Not a great uuid, huh? Good enough for us, this only has to be
			                           // unique across say an hour period max.
			handEuroFiltersInit(&history_new, FCMIN_HAND, FCMIN_D_HAND, BETA_HAND);
			history_new.last_hands_unfiltered.push_back(hands_unfiltered[i]);
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
		htExitFrame(htd, true, NULL, 0);
		return;
	}

	size_t num_hands = htd->histories_3d.size();
	// if (num_hands > 2) {
	HT_DEBUG(htd, "Ending with %zu hands!",
	         num_hands); // this is quite bad, but rarely happens.
	                     // }

	// Here, we go back to our bbox_histories and remove the histories for any bounding boxes that never turned into
	// good hands.

	// Iterate over all hands we're keeping track of, compute their current handedness.
	std::vector<size_t> valid_2d_idxs[2];


	for (size_t i = 0; i < htd->histories_3d.size(); i++) {
		// U_LOG_E("Valid hand %zu l_idx %i r_idx %i", i, htd->histories_3d[i].last_hands[0]->idx_l,
		//         htd->histories_3d[i].last_hands[0]->idx_r);
		valid_2d_idxs[0].push_back(htd->histories_3d[i].last_hands_unfiltered.back().idx_l);
		valid_2d_idxs[1].push_back(htd->histories_3d[i].last_hands_unfiltered.back().idx_r);
		handednessHandHistory3D(&htd->histories_3d[i]);
	}

	// Almost certainly not the cleanest way of doing this but leave me alone
	// Per camera view
	for (int view = 0; view < 2; view++) {
		// Per entry in bbox_histories
		for (size_t hist_idx = 0; hist_idx < htd->views[view].bbox_histories.size(); hist_idx++) {
			// See if this entry in bbox_histories ever turned into a 3D hand. If not, we notify (in a very
			// silly way) htImageToKeypoints that it should go away because it was an erroneous detection.
			for (size_t valid_idx : valid_2d_idxs[view]) {
				if (valid_idx == hist_idx) {
					htd->views[view].bbox_histories[hist_idx].htAlgorithm_approves = true;
					break;
				} else {
					htd->views[view].bbox_histories[hist_idx].htAlgorithm_approves = false;
				}
			}
		}
	}

	// Whoo! Okay, now we have some unfiltered hands in htd->histories_3d[i].last_hands[0]! Euro filter them!

	std::vector<Hand3D> filtered_hands(num_hands);

	for (size_t hand_index = 0; hand_index < num_hands; hand_index++) {
		handEuroFiltersRun(htd, &htd->histories_3d[hand_index], &filtered_hands[hand_index]);
		htd->histories_3d[hand_index].last_hands_filtered.push_back(filtered_hands[hand_index]);
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



		htProcessJoint(htd, palm, put_in_set, XRT_HAND_JOINT_PALM);

		htProcessJoint(htd, hand->kps[0], put_in_set, XRT_HAND_JOINT_WRIST);
		htProcessJoint(htd, hand->kps[1], put_in_set, XRT_HAND_JOINT_THUMB_METACARPAL);
		htProcessJoint(htd, hand->kps[2], put_in_set, XRT_HAND_JOINT_THUMB_PROXIMAL);
		htProcessJoint(htd, hand->kps[3], put_in_set, XRT_HAND_JOINT_THUMB_DISTAL);
		htProcessJoint(htd, hand->kps[4], put_in_set, XRT_HAND_JOINT_THUMB_TIP);

		htProcessJoint(htd, index_metacarpal, put_in_set, XRT_HAND_JOINT_INDEX_METACARPAL);
		htProcessJoint(htd, hand->kps[5], put_in_set, XRT_HAND_JOINT_INDEX_PROXIMAL);
		htProcessJoint(htd, hand->kps[6], put_in_set, XRT_HAND_JOINT_INDEX_INTERMEDIATE);
		htProcessJoint(htd, hand->kps[7], put_in_set, XRT_HAND_JOINT_INDEX_DISTAL);
		htProcessJoint(htd, hand->kps[8], put_in_set, XRT_HAND_JOINT_INDEX_TIP);

		htProcessJoint(htd, middle_metacarpal, put_in_set, XRT_HAND_JOINT_MIDDLE_METACARPAL);
		htProcessJoint(htd, hand->kps[9], put_in_set, XRT_HAND_JOINT_MIDDLE_PROXIMAL);
		htProcessJoint(htd, hand->kps[10], put_in_set, XRT_HAND_JOINT_MIDDLE_INTERMEDIATE);
		htProcessJoint(htd, hand->kps[11], put_in_set, XRT_HAND_JOINT_MIDDLE_DISTAL);
		htProcessJoint(htd, hand->kps[12], put_in_set, XRT_HAND_JOINT_MIDDLE_TIP);

		htProcessJoint(htd, ring_metacarpal, put_in_set, XRT_HAND_JOINT_RING_METACARPAL);
		htProcessJoint(htd, hand->kps[13], put_in_set, XRT_HAND_JOINT_RING_PROXIMAL);
		htProcessJoint(htd, hand->kps[14], put_in_set, XRT_HAND_JOINT_RING_INTERMEDIATE);
		htProcessJoint(htd, hand->kps[15], put_in_set, XRT_HAND_JOINT_RING_DISTAL);
		htProcessJoint(htd, hand->kps[16], put_in_set, XRT_HAND_JOINT_RING_TIP);

		htProcessJoint(htd, pinky_metacarpal, put_in_set, XRT_HAND_JOINT_LITTLE_METACARPAL);
		htProcessJoint(htd, hand->kps[17], put_in_set, XRT_HAND_JOINT_LITTLE_PROXIMAL);
		htProcessJoint(htd, hand->kps[18], put_in_set, XRT_HAND_JOINT_LITTLE_INTERMEDIATE);
		htProcessJoint(htd, hand->kps[19], put_in_set, XRT_HAND_JOINT_LITTLE_DISTAL);
		htProcessJoint(htd, hand->kps[20], put_in_set, XRT_HAND_JOINT_LITTLE_TIP);

		put_in_set->is_active = true;
		math_pose_identity(&put_in_set->hand_pose.pose);


		put_in_set->hand_pose.pose.orientation = htd->stereo_camera_to_left_camera;

		put_in_set->hand_pose.relation_flags = valid_flags_ht;

		applyJointWidths(put_in_set);
		applyJointOrientations(put_in_set, xr_indices[i]);
	}

	htExitFrame(htd, false, final_hands_ordered_by_handedness, filtered_hands[0].timestamp);
}
