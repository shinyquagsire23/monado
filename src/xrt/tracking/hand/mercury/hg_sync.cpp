// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Mercury hand tracking main file.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <jakob@collabora.com>
 * @author Nick Klingensmith <programmerpichu@gmail.com>
 * @ingroup tracking
 */

#include "hg_sync.hpp"
#include "hg_model.hpp"
#include "util/u_sink.h"
#include <numeric>

namespace xrt::tracking::hand::mercury {
// Flags to tell state tracker that these are indeed valid joints
static const enum xrt_space_relation_flags valid_flags_ht = (enum xrt_space_relation_flags)(
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

static void
hgJointDisparityMath(struct HandTracking *htd, Hand2D *hand_in_left, Hand2D *hand_in_right, Hand3D *out_hand)
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
	HT_DEBUG(htd, "I think the baseline is %f meters!", htd->baseline);
	// Note, this assumes camera 0 is the left camera and camera 1 is the right camera.
	// If you find one with the opposite arrangement, you'll need to invert htd->baseline, and look at
	// hgJointDisparityMath

	htd->use_fisheye = wrap.view[0].use_fisheye;

	if (htd->use_fisheye) {
		HT_DEBUG(htd, "I think the cameras are fisheye!");
	} else {
		HT_DEBUG(htd, "I think the cameras are not fisheye!");
	}

	cv::Matx34d P1;
	cv::Matx34d P2;

	cv::Matx44d Q;

	// We only want R1 and R2, we don't care about anything else
	if (htd->use_fisheye) {
		cv::fisheye::stereoRectify(wrap.view[0].intrinsics_mat,                  // cameraMatrix1
		                           wrap.view[0].distortion_fisheye_mat,          // distCoeffs1
		                           wrap.view[1].intrinsics_mat,                  // cameraMatrix2
		                           wrap.view[1].distortion_fisheye_mat,          // distCoeffs2
		                           wrap.view[0].image_size_pixels_cv,            // imageSize*
		                           wrap.camera_rotation_mat,                     // R
		                           wrap.camera_translation_mat,                  // T
		                           htd->views[0].rotate_camera_to_stereo_camera, // R1
		                           htd->views[1].rotate_camera_to_stereo_camera, // R2
		                           P1,                                           // P1
		                           P2,                                           // P2
		                           Q,                                            // Q
		                           0,                                            // flags
		                           cv::Size());                                  // newImageSize
	} else {
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
	}

	//* Good enough guess that view 0 and view 1 are the same size.
	for (int i = 0; i < 2; i++) {
		htd->views[i].cameraMatrix = wrap.view[i].intrinsics_mat;

		if (htd->use_fisheye) {
			htd->views[i].distortion = wrap.view[i].distortion_fisheye_mat;
		} else {
			htd->views[i].distortion = wrap.view[i].distortion_mat;
		}

		if (htd->log_level <= U_LOGGING_DEBUG) {
			HT_DEBUG(htd, "R%d ->", i);
			std::cout << htd->views[i].rotate_camera_to_stereo_camera << std::endl;

			HT_DEBUG(htd, "K%d ->", i);
			std::cout << htd->views[i].cameraMatrix << std::endl;

			HT_DEBUG(htd, "D%d ->", i);
			std::cout << htd->views[i].distortion << std::endl;
		}
	}

	htd->calibration_one_view_size_px.w = wrap.view[0].image_size_pixels.w;
	htd->calibration_one_view_size_px.h = wrap.view[0].image_size_pixels.h;

	htd->last_frame_one_view_size_px = htd->calibration_one_view_size_px;
	htd->multiply_px_coord_for_undistort = 1.0f;

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

	return true;
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
		strcpy(htd->models_folder, xdg_home);
	} else if (home != NULL) {
		strcpy(htd->models_folder, home);
	} else {
		assert(false);
	}
	strcat(htd->models_folder, "/.local/share/monado/hand-tracking-models/");
#endif
}


static void
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

static void
applyJointWidths(struct HandTracking *htd, struct xrt_hand_joint_set *set)
{
	// Thanks to Nick Klingensmith for this idea
	struct xrt_hand_joint_value *gr = set->values.hand_joint_set_default;

	static const float finger_joint_size[5] = {0.022f, 0.021f, 0.022f, 0.021f, 0.02f};
	static const float hand_finger_size[5] = {1.0f, 1.0f, 0.83f, 0.75f};

	static const float thumb_size[4] = {0.016f, 0.014f, 0.012f, 0.012f};
	float mul = 1.0f;


	for (int i = XRT_HAND_JOINT_THUMB_METACARPAL; i <= XRT_HAND_JOINT_THUMB_TIP; i++) {
		int j = i - XRT_HAND_JOINT_THUMB_METACARPAL;
		gr[i].radius = thumb_size[j] * mul;
	}

	for (int finger = 0; finger < 4; finger++) {
		for (int joint = 0; joint < 5; joint++) {
			int set_idx = finger * 5 + joint + XRT_HAND_JOINT_INDEX_METACARPAL;
			float val = finger_joint_size[joint] * hand_finger_size[finger] * .5 * mul;
			gr[set_idx].radius = val;
		}
	}
	// The radius of each joint is the distance from the joint to the skin in meters. -OpenXR spec.
	set->values.hand_joint_set_default[XRT_HAND_JOINT_PALM].radius =
	    .032f * .5f; // Measured my palm thickness with calipers
	set->values.hand_joint_set_default[XRT_HAND_JOINT_WRIST].radius =
	    .040f * .5f; // Measured my wrist thickness with calipers
}

static bool
handle_changed_image_size(HandTracking *htd, xrt_size &new_one_view_size)
{
	int gcd_calib = std::gcd(htd->calibration_one_view_size_px.h, htd->calibration_one_view_size_px.w);
	int gcd_new = std::gcd(new_one_view_size.h, new_one_view_size.w);

	int lcm_h_calib = htd->calibration_one_view_size_px.h / gcd_calib;
	int lcm_w_calib = htd->calibration_one_view_size_px.w / gcd_calib;

	int lcm_h_new = new_one_view_size.h / gcd_new;
	int lcm_w_new = new_one_view_size.w / gcd_new;

	bool good = (lcm_h_calib == lcm_h_new) && (lcm_w_calib == lcm_w_new);

	if (!good) {
		HT_WARN(htd, "Can't process this frame, wrong aspect ratio. What we wanted: %dx%d, what we got: %dx%d",
		        lcm_h_calib, lcm_w_calib, lcm_h_new, lcm_w_new);
		return false;
	}

	htd->multiply_px_coord_for_undistort = (float)htd->calibration_one_view_size_px.h / (float)new_one_view_size.h;
	htd->last_frame_one_view_size_px = new_one_view_size;
	return true;
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
	u_sink_debug_init(&this->debug_sink);
}

HandTracking::~HandTracking()
{
	u_sink_debug_destroy(&this->debug_sink);

	release_onnx_wrap(&this->views[0].keypoint[0]);
	release_onnx_wrap(&this->views[0].keypoint[1]);
	release_onnx_wrap(&this->views[0].detection);


	release_onnx_wrap(&this->views[1].keypoint[0]);
	release_onnx_wrap(&this->views[1].keypoint[1]);
	release_onnx_wrap(&this->views[1].detection);

	u_worker_group_reference(&this->group, NULL);

	t_stereo_camera_calibration_reference(&this->calib, NULL);

	free_kinematic_hand(&this->kinematic_hands[0]);
	free_kinematic_hand(&this->kinematic_hands[1]);

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

	HandTracking *htd = (struct HandTracking *)ht_sync;

	htd->current_frame_timestamp = left_frame->timestamp;

	struct xrt_hand_joint_set *out_xrt_hands[2] = {out_left_hand, out_right_hand};


	/*
	 * Setup views.
	 */

	assert(left_frame->width == right_frame->width);
	assert(left_frame->height == right_frame->height);

	const int full_height = left_frame->height;
	const int full_width = left_frame->width * 2;

	if ((left_frame->width != (uint32_t)htd->last_frame_one_view_size_px.w) ||
	    (left_frame->height != (uint32_t)htd->last_frame_one_view_size_px.h)) {
		xrt_size new_one_view_size;
		new_one_view_size.h = left_frame->height;
		new_one_view_size.w = left_frame->width;
		// Could be an assert, should never happen.
		if (!handle_changed_image_size(htd, new_one_view_size)) {
			return;
		}
	}

	const int view_width = htd->last_frame_one_view_size_px.w;
	const int view_height = htd->last_frame_one_view_size_px.h;

	const cv::Size full_size = cv::Size(full_width, full_height);
	const cv::Size view_size = cv::Size(view_width, view_height);
	const cv::Point view_offsets[2] = {cv::Point(0, 0), cv::Point(view_width, 0)};

	htd->views[0].run_model_on_this = cv::Mat(view_size, CV_8UC1, left_frame->data, left_frame->stride);
	htd->views[1].run_model_on_this = cv::Mat(view_size, CV_8UC1, right_frame->data, right_frame->stride);


	*out_timestamp_ns = htd->current_frame_timestamp; // No filtering, fine to do this now. Also just a reminder
	                                                  // that this took you 2 HOURS TO DEBUG THAT ONE TIME.

	htd->debug_scribble = u_sink_debug_is_active(&htd->debug_sink);

	cv::Mat debug_output = {};
	xrt_frame *debug_frame = nullptr;


	if (htd->debug_scribble) {
		u_frame_create_one_off(XRT_FORMAT_R8G8B8, full_width, full_height, &debug_frame);
		debug_frame->timestamp = htd->current_frame_timestamp;

		debug_output = cv::Mat(full_size, CV_8UC3, debug_frame->data, debug_frame->stride);

		cv::cvtColor(htd->views[0].run_model_on_this, debug_output(cv::Rect(view_offsets[0], view_size)),
		             cv::COLOR_GRAY2BGR);
		cv::cvtColor(htd->views[1].run_model_on_this, debug_output(cv::Rect(view_offsets[1], view_size)),
		             cv::COLOR_GRAY2BGR);

		htd->views[0].debug_out_to_this = debug_output(cv::Rect(view_offsets[0], view_size));
		htd->views[1].debug_out_to_this = debug_output(cv::Rect(view_offsets[1], view_size));
	}

	u_worker_group_push(htd->group, run_hand_detection, &htd->views[0]);
	u_worker_group_push(htd->group, run_hand_detection, &htd->views[1]);
	u_worker_group_wait_all(htd->group);



	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {

		if (!(htd->views[0].det_outputs[hand_idx].found && htd->views[1].det_outputs[hand_idx].found)) {
			// If we don't find this hand in *both* views
			out_xrt_hands[hand_idx]->is_active = false;
			continue;
		}
		out_xrt_hands[hand_idx]->is_active = true;


		for (int view_idx = 0; view_idx < 2; view_idx++) {
			struct keypoint_estimation_run_info &inf = htd->views[view_idx].run_info[hand_idx];
			inf.view = &htd->views[view_idx];
			inf.hand_idx = hand_idx;
			u_worker_group_push(htd->group, htd->keypoint_estimation_run_func,
			                    &htd->views[view_idx].run_info[hand_idx]);
		}
	}
	u_worker_group_wait_all(htd->group);

	for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
		if (!out_xrt_hands[hand_idx]->is_active) {
			htd->last_frame_hand_detected[hand_idx] = false;
			continue;
		}
		kine::kinematic_hand_4f *hand = htd->kinematic_hands[hand_idx];
		if (!htd->last_frame_hand_detected[hand_idx]) {
			kine::init_hardcoded_statics(hand, htd->hand_size / 100.0f);
		}



		Hand2D *hand_in_left_view = &htd->views[0].keypoint_outputs[hand_idx].hand_tan_space;
		Hand2D *hand_in_right_view = &htd->views[1].keypoint_outputs[hand_idx].hand_tan_space;
		Hand3D hand_3d;



		struct xrt_hand_joint_set *put_in_set = out_xrt_hands[hand_idx];

		applyThumbIndexDrag(&hand_3d);

		applyJointWidths(htd, put_in_set);

		hgJointDisparityMath(htd, hand_in_left_view, hand_in_right_view, &hand_3d);

		kine::optimize_new_frame(hand_3d.kps, hand, put_in_set, hand_idx);


		math_pose_identity(&put_in_set->hand_pose.pose);

		switch (htd->output_space) {
		case HT_OUTPUT_SPACE_LEFT_CAMERA: {
			put_in_set->hand_pose.pose.orientation = htd->stereo_camera_to_left_camera;
		} break;
		case HT_OUTPUT_SPACE_CENTER_OF_STEREO_CAMERA: {
			put_in_set->hand_pose.pose.orientation.w = 1.0;
			put_in_set->hand_pose.pose.position.x = -htd->baseline / 2;
		} break;
		}

		put_in_set->hand_pose.relation_flags = valid_flags_ht;
	}



	u_frame_times_widget_push_sample(&htd->ft_widget, htd->current_frame_timestamp);

	if (htd->debug_scribble) {
		u_sink_debug_push_frame(&htd->debug_sink, debug_frame);
		xrt_frame_reference(&debug_frame, NULL);
	}
}

void
HandTracking::cCallbackDestroy(t_hand_tracking_sync *ht_sync)
{
	HandTracking *ht_ptr = &HandTracking::fromC(ht_sync);

	delete ht_ptr;
}

} // namespace xrt::tracking::hand::mercury


/*
 *
 * 'Exported' functions.
 *
 */

extern "C" t_hand_tracking_sync *
t_hand_tracking_sync_mercury_create(struct t_stereo_camera_calibration *calib, hand_tracking_output_space output_space)
{
	XRT_TRACE_MARKER();

	auto htd = new xrt::tracking::hand::mercury::HandTracking();

	// Setup logging first. We like logging.
	htd->log_level = xrt::tracking::hand::mercury::debug_get_log_option_mercury_log();
	bool use_simdr = xrt::tracking::hand::mercury::debug_get_bool_option_mercury_use_simdr_keypoint();

	htd->output_space = output_space;

	/*
	 * Get configuration
	 */

	assert(calib != NULL);
	htd->calib = NULL;
	// We have to reference it, getCalibration points at it.
	t_stereo_camera_calibration_reference(&htd->calib, calib);
	getCalibration(htd, calib);
	getModelsFolder(htd);

#ifdef USE_NCNN
	{
		htd->net = ncnn_net_create();
		ncnn_option_t opt = ncnn_option_create();
		ncnn_option_set_use_vulkan_compute(opt, 1);

		ncnn_net_set_option(htd->net, opt);
		ncnn_net_load_param(htd->net, "/3/clones/ncnn/batch_size_2.param");
		ncnn_net_load_model(htd->net, "/3/clones/ncnn/batch_size_2.bin");
	}

	{
		htd->net_keypoint = ncnn_net_create();
		ncnn_option_t opt = ncnn_option_create();
		ncnn_option_set_use_vulkan_compute(opt, 1);

		ncnn_net_set_option(htd->net_keypoint, opt);
		ncnn_net_load_param(
		    htd->net_keypoint,
		    "/home/moses/.local/share/monado/hand-tracking-models/grayscale_keypoint_new.param");
		ncnn_net_load_model(htd->net_keypoint,
		                    "/home/moses/.local/share/monado/hand-tracking-models/grayscale_keypoint_new.bin");
	}


#endif

	htd->views[0].htd = htd;
	htd->views[1].htd = htd; // :)

	init_hand_detection(htd, &htd->views[0].detection);
	init_hand_detection(htd, &htd->views[1].detection);

	if (use_simdr) {
		init_keypoint_estimation(htd, &htd->views[0].keypoint[0]);
		init_keypoint_estimation(htd, &htd->views[0].keypoint[1]);

		init_keypoint_estimation(htd, &htd->views[1].keypoint[0]);
		init_keypoint_estimation(htd, &htd->views[1].keypoint[1]);
		htd->keypoint_estimation_run_func = xrt::tracking::hand::mercury::run_keypoint_estimation;
	} else {
		init_keypoint_estimation_new(htd, &htd->views[0].keypoint[0]);
		init_keypoint_estimation_new(htd, &htd->views[0].keypoint[1]);

		init_keypoint_estimation_new(htd, &htd->views[1].keypoint[0]);
		init_keypoint_estimation_new(htd, &htd->views[1].keypoint[1]);
		htd->keypoint_estimation_run_func = xrt::tracking::hand::mercury::run_keypoint_estimation_new;
	}

	htd->views[0].view = 0;
	htd->views[1].view = 1;

	int num_threads = 4;
	htd->pool = u_worker_thread_pool_create(num_threads - 1, num_threads);
	htd->group = u_worker_group_create(htd->pool);

	htd->hand_size = 9.5864;
	xrt::tracking::hand::mercury::kine::alloc_kinematic_hand(&htd->kinematic_hands[0]);
	xrt::tracking::hand::mercury::kine::alloc_kinematic_hand(&htd->kinematic_hands[1]);

	u_frame_times_widget_init(&htd->ft_widget, 10.0f, 10.0f);

	u_var_add_root(htd, "Camera-based Hand Tracker", true);


	u_var_add_f32(htd, &htd->hand_size, "Hand size (Centimeters between wrist and middle-proximal joint)");
	u_var_add_ro_f32(htd, &htd->ft_widget.fps, "FPS!");
	u_var_add_f32_timing(htd, htd->ft_widget.debug_var, "times!");

	u_var_add_sink_debug(htd, &htd->debug_sink, "i");

	HT_DEBUG(htd, "Hand Tracker initialized!");

	return &htd->base;
}
