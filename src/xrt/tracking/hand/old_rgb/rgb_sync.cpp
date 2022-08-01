// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Old RGB hand tracking main file.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#include "rgb_interface.h"
#include "rgb_sync.hpp"
#include "xrt/xrt_frame.h"


using namespace xrt::tracking::hand::old_rgb;



#include "xrt/xrt_defines.h"

#include "math/m_vec2.h"
#include "util/u_frame.h"
#include "util/u_trace_marker.h"


#include "templates/NaivePermutationSort.hpp"

#include <future>


// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking driver code.
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ht
 */

#if defined(EXPERIMENTAL_DATASET_RECORDING)
#include "gstreamer/gst_pipeline.h"
#include "gstreamer/gst_sink.h"
#endif

#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_frameserver.h"

#include "os/os_time.h"
#include "os/os_threading.h"

#include "math/m_api.h"
#include "math/m_eigen_interop.hpp"

#include "util/u_device.h"
#include "util/u_frame.h"
#include "util/u_hand_tracking.h"
#include "util/u_sink.h"
#include "util/u_format.h"
#include "util/u_logging.h"
#include "util/u_time.h"
#include "util/u_trace_marker.h"
#include "util/u_time.h"
#include "util/u_json.h"
#include "util/u_config_json.h"

#include "tracking/t_frame_cv_mat_wrapper.hpp"
#include "tracking/t_calibration_opencv.hpp"

#include "rgb_hand_math.hpp"
#include "rgb_image_math.hpp"
#include "rgb_model.hpp"

#include <cjson/cJSON.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/calib3d.hpp>

#include <math.h>
#include <float.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <cmath>

#include <limits>
#include <thread>
#include <future>
#include <fstream>
#include <numeric>
#include <sstream>
#include <iostream>
#include <exception>
#include <algorithm>



// Flags to tell state tracker that these are indeed valid joints
static const enum xrt_space_relation_flags valid_flags_ht = (enum xrt_space_relation_flags)(
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);


static void
htProcessJoint(struct HandTracking *htd,
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
errHistory2D(const HandHistory2DBBox &past, const Palm7KP &present)
{
	if (!past.htAlgorithm_approves) {
		// U_LOG_E("Returning big number because htAlgorithm told me to!");
		return 100000000000000000000000000000.0f;
	}
	float sum_of_lengths = m_vec2_len(past.wrist_unfiltered.back() - past.middle_unfiltered.back()) +
	                       m_vec2_len(present.kps[WRIST_7KP] - present.kps[MIDDLE_7KP]);

	float sum_of_distances = (m_vec2_len(past.wrist_unfiltered.back() - present.kps[WRIST_7KP]) +
	                          m_vec2_len(past.middle_unfiltered.back() - present.kps[MIDDLE_7KP]));


	float final = sum_of_distances / sum_of_lengths;

	return final;
}

static std::vector<Hand2D>
htImageToKeypoints(struct ht_view *htv)
{
	struct HandTracking *htd = htv->htd;
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
jsonMaybeAddSomeHands(struct HandTracking *htd, bool err)
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
htJointDisparityMath(struct HandTracking *htd, Hand2D *hand_in_left, Hand2D *hand_in_right, Hand3D *out_hand)
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

DEBUG_GET_ONCE_LOG_OPTION(ht_log, "HT_LOG", U_LOGGING_WARN)

/*!
 * Setup helper functions.
 */

static bool
getCalibration(struct HandTracking *htd, t_stereo_camera_calibration *calibration)
{
	xrt::auxiliary::tracking::StereoCameraCalibrationWrapper wrap(calibration);
	xrt_vec3 trans = {(float)wrap.camera_translation_mat(0, 0), (float)wrap.camera_translation_mat(1, 0),
	                  (float)wrap.camera_translation_mat(2, 0)};
	htd->baseline = m_vec3_len(trans);

#if 0
	std::cout << "\n\nTRANSLATION VECTOR IS\n" << wrap.camera_translation_mat;
	std::cout << "\n\nROTATION FROM LEFT TO RIGHT IS\n" << wrap.camera_rotation_mat << "\n";
#endif

	cv::Matx34d P1;
	cv::Matx34d P2;

	cv::Matx44d Q;

	// The only reason we're calling stereoRectify is because we want R1 and R2 for the
	cv::stereoRectify(wrap.view[0].intrinsics_mat,                  // cameraMatrix1
	                  wrap.view[0].distortion_mat,                  // distCoeffs1
	                  wrap.view[1].intrinsics_mat,                  // cameraMatrix2
	                  wrap.view[1].distortion_mat,                  // distCoeffs2
	                  wrap.view[0].image_size_pixels_cv,            // imageSize*
	                  wrap.camera_rotation_mat,                     // R
	                  wrap.camera_translation_mat,                  // T
	                  htd->views[0].rotate_camera_to_stereo_camera, // R1
	                  htd->views[1].rotate_camera_to_stereo_camera, // R2
	                  P1,                                           // P1
	                  P2,                                           // P2
	                  Q,                                            // Q
	                  0,                                            // flags
	                  -1.0f,                                        // alpha
	                  cv::Size(),                                   // newImageSize
	                  NULL,                                         // validPixROI1
	                  NULL);                                        // validPixROI2

	//* Good enough guess that view 0 and view 1 are the same size.

	for (int i = 0; i < 2; i++) {
		htd->views[i].cameraMatrix = wrap.view[i].intrinsics_mat;

		htd->views[i].distortion = wrap.view[i].distortion_fisheye_mat;
	}

	htd->one_view_size_px.w = wrap.view[0].image_size_pixels.w;
	htd->one_view_size_px.h = wrap.view[0].image_size_pixels.h;

	U_LOG_E("%d %d %p %p", htd->one_view_size_px.w, htd->one_view_size_px.h, (void *)&htd->one_view_size_px.w,
	        (void *)&htd->one_view_size_px.h);



	cv::Matx33d rotate_stereo_camera_to_left_camera = htd->views[0].rotate_camera_to_stereo_camera.inv();

	xrt_matrix_3x3 s;
	s.v[0] = rotate_stereo_camera_to_left_camera(0, 0);
	s.v[1] = rotate_stereo_camera_to_left_camera(0, 1);
	s.v[2] = rotate_stereo_camera_to_left_camera(0, 2);

	s.v[3] = rotate_stereo_camera_to_left_camera(1, 0);
	s.v[4] = rotate_stereo_camera_to_left_camera(1, 1);
	s.v[5] = rotate_stereo_camera_to_left_camera(1, 2);

	s.v[6] = rotate_stereo_camera_to_left_camera(2, 0);
	s.v[7] = rotate_stereo_camera_to_left_camera(2, 1);
	s.v[8] = rotate_stereo_camera_to_left_camera(2, 2);

	xrt_quat tmp;

	math_quat_from_matrix_3x3(&s, &tmp);

	// Weird that I have to invert this quat, right? I think at some point - like probably just before this - I must
	// have swapped row-major and col-major - remember, if you transpose a rotation matrix, you get its inverse.
	// Doesn't matter that I don't understand - non-inverted looks definitely wrong, inverted looks definitely
	// right.
	math_quat_invert(&tmp, &htd->stereo_camera_to_left_camera);

#if 0
	U_LOG_E("%f %f %f %f", htd->stereo_camera_to_left_camera.w, htd->stereo_camera_to_left_camera.x,
	        htd->stereo_camera_to_left_camera.y, htd->stereo_camera_to_left_camera.z);
#endif

	return true;
}

#if 0
static void
getStartupConfig(struct HandTracking *htd, const cJSON *startup_config)
{
	const cJSON *palm_detection_type = u_json_get(startup_config, "palm_detection_model");
	const cJSON *keypoint_estimation_type = u_json_get(startup_config, "keypoint_estimation_model");
	const cJSON *uvc_wire_format = u_json_get(startup_config, "uvc_wire_format");

	// IsString does its own null-checking
	if (cJSON_IsString(palm_detection_type)) {
		bool is_collabora = (strcmp(cJSON_GetStringValue(palm_detection_type), "collabora") == 0);
		bool is_mediapipe = (strcmp(cJSON_GetStringValue(palm_detection_type), "mediapipe") == 0);
		if (!is_collabora && !is_mediapipe) {
			HT_WARN(htd, "Unknown palm detection type %s - should be \"collabora\" or \"mediapipe\"",
			        cJSON_GetStringValue(palm_detection_type));
		}
		htd->startup_config.palm_detection_use_mediapipe = is_mediapipe;
	}

	if (cJSON_IsString(keypoint_estimation_type)) {
		bool is_collabora = (strcmp(cJSON_GetStringValue(keypoint_estimation_type), "collabora") == 0);
		bool is_mediapipe = (strcmp(cJSON_GetStringValue(keypoint_estimation_type), "mediapipe") == 0);
		if (!is_collabora && !is_mediapipe) {
			HT_WARN(htd, "Unknown keypoint estimation type %s - should be \"collabora\" or \"mediapipe\"",
			        cJSON_GetStringValue(keypoint_estimation_type));
		}
		htd->startup_config.keypoint_estimation_use_mediapipe = is_mediapipe;
	}

	if (cJSON_IsString(uvc_wire_format)) {
		bool is_yuv = (strcmp(cJSON_GetStringValue(uvc_wire_format), "yuv") == 0);
		bool is_mjpeg = (strcmp(cJSON_GetStringValue(uvc_wire_format), "mjpeg") == 0);
		if (!is_yuv && !is_mjpeg) {
			HT_WARN(htd, "Unknown wire format type %s - should be \"yuv\" or \"mjpeg\"",
			        cJSON_GetStringValue(uvc_wire_format));
		}
		if (is_yuv) {
			HT_DEBUG(htd, "Using YUYV422!");
			htd->startup_config.desired_format = XRT_FORMAT_YUYV422;
		} else {
			HT_DEBUG(htd, "Using MJPEG!");
			htd->startup_config.desired_format = XRT_FORMAT_MJPEG;
		}
	}
}

static void
getUserConfig(struct HandTracking *htd)
{
	// The game here is to avoid bugs + be paranoid, not to be fast. If you see something that seems "slow" - don't
	// fix it. Any of the tracking code is way stickier than this could ever be.

	struct u_config_json config_json = {};

	u_config_json_open_or_create_main_file(&config_json);
	if (!config_json.file_loaded) {
		return;
	}

	cJSON *ht_config_json = cJSON_GetObjectItemCaseSensitive(config_json.root, "config_ht");
	if (ht_config_json == NULL) {
		return;
	}

	// Don't get it twisted: initializing these to NULL is not cargo-culting.
	// Uninitialized values on the stack aren't guaranteed to be 0, so these could end up pointing to what we
	// *think* is a valid address but what is *not* one.
	char *startup_config_string = NULL;
	char *dynamic_config_string = NULL;

	{
		const cJSON *startup_config_string_json = u_json_get(ht_config_json, "startup_config_index");
		if (cJSON_IsString(startup_config_string_json)) {
			startup_config_string = cJSON_GetStringValue(startup_config_string_json);
		}

		const cJSON *dynamic_config_string_json = u_json_get(ht_config_json, "dynamic_config_index");
		if (cJSON_IsString(dynamic_config_string_json)) {
			dynamic_config_string = cJSON_GetStringValue(dynamic_config_string_json);
		}
	}

	if (startup_config_string != NULL) {
		const cJSON *startup_config_obj =
		    u_json_get(u_json_get(ht_config_json, "startup_configs"), startup_config_string);
		getStartupConfig(htd, startup_config_obj);
	}

	if (dynamic_config_string != NULL) {
		const cJSON *dynamic_config_obj =
		    u_json_get(u_json_get(ht_config_json, "dynamic_configs"), dynamic_config_string);
		{
			ht_dynamic_config *hdc = &htd->dynamic_config;
			// Do the thing
			u_json_get_string_into_array(u_json_get(dynamic_config_obj, "name"), hdc->name, 64);

			u_json_get_float(u_json_get(dynamic_config_obj, "hand_fc_min"), &hdc->hand_fc_min.val);
			u_json_get_float(u_json_get(dynamic_config_obj, "hand_fc_min_d"), &hdc->hand_fc_min_d.val);
			u_json_get_float(u_json_get(dynamic_config_obj, "hand_beta"), &hdc->hand_beta.val);

			u_json_get_float(u_json_get(dynamic_config_obj, "nms_iou"), &hdc->nms_iou.val);
			u_json_get_float(u_json_get(dynamic_config_obj, "nms_threshold"), &hdc->nms_threshold.val);

			u_json_get_bool(u_json_get(dynamic_config_obj, "scribble_nms_detections"),
			                &hdc->scribble_nms_detections);
			u_json_get_bool(u_json_get(dynamic_config_obj, "scribble_raw_detections"),
			                &hdc->scribble_raw_detections);
			u_json_get_bool(u_json_get(dynamic_config_obj, "scribble_2d_keypoints"),
			                &hdc->scribble_2d_keypoints);
			u_json_get_bool(u_json_get(dynamic_config_obj, "scribble_bounding_box"),
			                &hdc->scribble_bounding_box);

			char *dco_str = cJSON_Print(dynamic_config_obj);
			U_LOG_D("Config %s %s", dynamic_config_string, dco_str);
			free(dco_str);
		}
	}



	cJSON_Delete(config_json.root);
	return;
}
#endif

static void
userConfigSetDefaults(struct HandTracking *htd)
{
	// Admit defeat: for now, Mediapipe's are still better than ours.
	htd->startup_config.palm_detection_use_mediapipe = true;
	htd->startup_config.keypoint_estimation_use_mediapipe = true;

	// Make sure you build DebugOptimized!
	htd->startup_config.desired_format = XRT_FORMAT_YUYV422;


	ht_dynamic_config *hdc = &htd->dynamic_config;

	hdc->scribble_nms_detections = true;
	hdc->scribble_raw_detections = false;
	hdc->scribble_2d_keypoints = true;
	hdc->scribble_bounding_box = false;

	hdc->hand_fc_min.min = 0.0f;
	hdc->hand_fc_min.max = 50.0f;
	hdc->hand_fc_min.step = 0.05f;
	hdc->hand_fc_min.val = FCMIN_HAND;

	hdc->hand_fc_min_d.min = 0.0f;
	hdc->hand_fc_min_d.max = 50.0f;
	hdc->hand_fc_min_d.step = 0.05f;
	hdc->hand_fc_min_d.val = FCMIN_D_HAND;


	hdc->hand_beta.min = 0.0f;
	hdc->hand_beta.max = 50.0f;
	hdc->hand_beta.step = 0.05f;
	hdc->hand_beta.val = BETA_HAND;

	hdc->max_vel.min = 0.0f;
	hdc->max_vel.max = 50.0f;
	hdc->max_vel.step = 0.05f;
	hdc->max_vel.val = 30.0f; // 30 m/s; about 108 kph. If your hand is going this fast, our tracking failing is the
	                          // least of your problems.

	hdc->max_acc.min = 0.0f;
	hdc->max_acc.max = 100.0f;
	hdc->max_acc.step = 0.1f;
	hdc->max_acc.val = 100.0f; // 100 m/s^2; about 10 Gs. Ditto.

	hdc->nms_iou.min = 0.0f;
	hdc->nms_iou.max = 1.0f;
	hdc->nms_iou.step = 0.01f;


	hdc->nms_threshold.min = 0.0f;
	hdc->nms_threshold.max = 1.0f;
	hdc->nms_threshold.step = 0.01f;

	hdc->new_detection_threshold.min = 0.0f;
	hdc->new_detection_threshold.max = 1.0f;
	hdc->new_detection_threshold.step = 0.01f;


	hdc->nms_iou.val = 0.05f;
	hdc->nms_threshold.val = 0.3f;
	hdc->new_detection_threshold.val = 0.6f;
}


static void
getModelsFolder(struct HandTracking *htd)
{
// Please bikeshed me on this! I don't know where is the best place to put this stuff!
#if 0
	char exec_location[1024] = {};
	readlink("/proc/self/exe", exec_location, 1024);

	HT_DEBUG(htd, "Exec at %s\n", exec_location);

	int end = 0;
	while (exec_location[end] != '\0') {
		HT_DEBUG(htd, "%d", end);
		end++;
	}

	while (exec_location[end] != '/' && end != 0) {
		HT_DEBUG(htd, "%d %c", end, exec_location[end]);
		exec_location[end] = '\0';
		end--;
	}

	strcat(exec_location, "../share/monado/hand-tracking-models/");
	strcpy(htd->startup_config.model_slug, exec_location);
#else
	const char *xdg_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xdg_home != NULL) {
		strcpy(htd->startup_config.model_slug, xdg_home);
	} else if (home != NULL) {
		strcpy(htd->startup_config.model_slug, home);
	} else {
		assert(false);
	}
	strcat(htd->startup_config.model_slug, "/.local/share/monado/hand-tracking-models/");
#endif
}



static void
htExitFrame(struct HandTracking *htd,
            bool err,
            struct xrt_hand_joint_set final_hands_ordered_by_handedness[2],
            uint64_t timestamp,
            struct xrt_hand_joint_set *out_left,
            struct xrt_hand_joint_set *out_right,
            uint64_t *out_timestamp_ns)
{

	os_mutex_lock(&htd->openxr_hand_data_mediator);
	*out_timestamp_ns = timestamp;

	if (err) {
		out_left->is_active = false;
		out_right->is_active = false;
	} else {
		*out_left = final_hands_ordered_by_handedness[0];
		*out_right = final_hands_ordered_by_handedness[1];


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

/*
 *
 * Member functions.
 *
 */

HandTracking::HandTracking()
{
	this->base.process = &HandTracking::cCallbackProcess;
	this->base.destroy = &HandTracking::cCallbackDestroy;
}

HandTracking::~HandTracking()
{
	//
}

//!@todo vVERY BAD
static void
combine_frames_r8g8b8_hack(struct xrt_frame *l, struct xrt_frame *r, struct xrt_frame *f)
{
	// SINK_TRACE_MARKER();

	uint32_t height = l->height;

	for (uint32_t y = 0; y < height; y++) {
		uint8_t *dst = f->data + f->stride * y;
		uint8_t *src = l->data + l->stride * y;

		for (uint32_t x = 0; x < l->width * 3; x++) {
			*dst++ = *src++;
		}

		dst = f->data + f->stride * y + l->width * 3;
		src = r->data + r->stride * y;
		for (uint32_t x = 0; x < r->width * 3; x++) {
			*dst++ = *src++;
		}
	}
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

	HandTracking *htd = (struct HandTracking *)ht_sync;

	// U_LOG_E("htd is at %p", htd);

	htd->current_frame_timestamp = left_frame->timestamp;

	int64_t start, end;
	start = os_monotonic_get_ns();


	/*
	 * Setup views.
	 */

	assert(left_frame->width == right_frame->width);
	assert(left_frame->height == right_frame->height);

	const int full_height = left_frame->height;
	const int full_width = left_frame->width * 2;

	const int view_width = htd->one_view_size_px.w;
	const int view_height = htd->one_view_size_px.h;

	assert(full_height == view_height);

	const cv::Size full_size = cv::Size(full_width, full_height);
	const cv::Size view_size = cv::Size(view_width, view_height);
	const cv::Point view_offsets[2] = {cv::Point(0, 0), cv::Point(view_width, 0)};

	// cv::Mat full_frame(full_size, CV_8UC3, htd->frame_for_process->data, htd->frame_for_process->stride);
	htd->views[0].run_model_on_this = cv::Mat(view_size, CV_8UC3, left_frame->data, left_frame->stride);
	htd->views[1].run_model_on_this = cv::Mat(view_size, CV_8UC3, right_frame->data, right_frame->stride);


	// Convenience
	uint64_t timestamp = left_frame->timestamp;

	htd->debug_scribble = u_sink_debug_is_active(&htd->debug_sink);

	cv::Mat debug_output = {};
	xrt_frame *debug_frame = nullptr;


	if (htd->debug_scribble) {
		u_frame_create_one_off(XRT_FORMAT_R8G8B8, full_width, full_height, &debug_frame);
		combine_frames_r8g8b8_hack(left_frame, right_frame, debug_frame);

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



	if (htd->debug_scribble) {
		u_sink_debug_push_frame(&htd->debug_sink, debug_frame);
		xrt_frame_reference(&debug_frame, NULL);
	}

	// Bail early this frame if no hands were detected.
	// In the long run, this'll be a silly thing - we shouldn't always take the detection model's word for it
	// especially when part of the pipeline is an arbitrary confidence threshold.
	if (hands_in_left_view.size() == 0 || hands_in_right_view.size() == 0) {
		htExitFrame(htd, true, NULL, timestamp, out_left_hand, out_right_hand, out_timestamp_ns);
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
			cur_hand.y_disparity_error = errHandDisparity(left_2d, right_2d);

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
			float errr = sumOfHandJointDistances(possible_3d_hands[idx_one], possible_3d_hands[idx_two]);
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
	// The preceding may not do anything, because we'll start out with no hand histories! All the numbers of
	// elements should be zero.


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
		htExitFrame(htd, true, NULL, timestamp, out_left_hand, out_right_hand, out_timestamp_ns);
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

		u_hand_joints_apply_joint_width(put_in_set);
		applyJointOrientations(put_in_set, xr_indices[i]);
	}
	htExitFrame(htd, false, final_hands_ordered_by_handedness, filtered_hands[0].timestamp, out_left_hand,
	            out_right_hand, out_timestamp_ns);
}

void
HandTracking::cCallbackDestroy(t_hand_tracking_sync *ht_sync)
{
	auto ht_ptr = &HandTracking::fromC(ht_sync);

	u_sink_debug_destroy(&ht_ptr->debug_sink);

	delete ht_ptr->views[0].htm;
	delete ht_ptr->views[1].htm;
	delete ht_ptr;
}


/*
 *
 * 'Exported' functions.
 *
 */

extern "C" t_hand_tracking_sync *
t_hand_tracking_sync_old_rgb_create(struct t_stereo_camera_calibration *calib)
{
	XRT_TRACE_MARKER();

	auto htd = new HandTracking();

	U_LOG_E("htd is at %p", (void *)htd);

	// Setup logging first. We like logging.
	htd->log_level = debug_get_log_option_ht_log();

	/*
	 * Get configuration
	 */

	u_sink_debug_init(&htd->debug_sink);
	assert(calib != NULL);
	getCalibration(htd, calib);
	// Set defaults - most people won't have a config json and it won't get past here.
	userConfigSetDefaults(htd);
	getModelsFolder(htd);


	htd->views[0].htd = htd;
	htd->views[1].htd = htd; // :)

	htd->views[0].htm = new ht_model(htd);
	htd->views[1].htm = new ht_model(htd);

	htd->views[0].view = 0;
	htd->views[1].view = 1;

	u_var_add_root(htd, "Camera-based Hand Tracker", true);

	u_var_add_draggable_f32(htd, &htd->dynamic_config.hand_fc_min, "hand_fc_min");
	u_var_add_draggable_f32(htd, &htd->dynamic_config.hand_fc_min_d, "hand_fc_min_d");
	u_var_add_draggable_f32(htd, &htd->dynamic_config.hand_beta, "hand_beta");
	u_var_add_draggable_f32(htd, &htd->dynamic_config.nms_iou, "nms_iou");
	u_var_add_draggable_f32(htd, &htd->dynamic_config.nms_threshold, "nms_threshold");
	u_var_add_draggable_f32(htd, &htd->dynamic_config.new_detection_threshold, "new_detection_threshold");

	u_var_add_bool(htd, &htd->dynamic_config.scribble_raw_detections, "Scribble raw detections");
	u_var_add_bool(htd, &htd->dynamic_config.scribble_nms_detections, "Scribble NMS detections");
	u_var_add_bool(htd, &htd->dynamic_config.scribble_2d_keypoints, "Scribble 2D keypoints");
	u_var_add_bool(htd, &htd->dynamic_config.scribble_bounding_box, "Scribble bounding box");

	u_var_add_sink_debug(htd, &htd->debug_sink, "i");


	HT_DEBUG(htd, "Hand Tracker initialized!");


	return &htd->base;
}
