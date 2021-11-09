// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking driver code.
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ht
 */

#include "gstreamer/gst_pipeline.h"
#include "gstreamer/gst_sink.h"
#include "ht_interface.h"
#include "ht_driver.hpp"

#include "../depthai/depthai_interface.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_frameserver.h"

#include "os/os_time.h"
#include "os/os_threading.h"

#include "math/m_api.h"
#include "math/m_eigen_interop.hpp"

#include "util/u_device.h"
#include "util/u_frame.h"
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

#include "ht_algorithm.hpp"
#include "ht_model.hpp"
#include "ht_models.hpp"

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

DEBUG_GET_ONCE_LOG_OPTION(ht_log, "HT_LOG", U_LOGGING_WARN)

/*!
 * Setup helper functions.
 */

static bool
getCalibration(struct ht_device *htd, t_stereo_camera_calibration *calibration)
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

	htd->camera.one_view_size_px.w = wrap.view[0].image_size_pixels.w;
	htd->camera.one_view_size_px.h = wrap.view[0].image_size_pixels.h;


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

	// Weird that I have to invert this quat, right? I think at some point - like probably just above this - I must
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

static void
getStartupConfig(struct ht_device *htd, const cJSON *startup_config)
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
getUserConfig(struct ht_device *htd)
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

			U_LOG_E("Hey %s %s", dynamic_config_string, cJSON_Print(dynamic_config_obj));
		}
	}



	cJSON_Delete(config_json.root);
	return;
}


static void
userConfigSetDefaults(struct ht_device *htd)
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
getModelsFolder(struct ht_device *htd)
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

#if defined(EXPERIMENTAL_DATASET_RECORDING)

static void
htStartJsonCB(void *ptr)
{
	struct ht_device *htd = (struct ht_device *)ptr;
	HT_INFO(htd, "Magic button pressed!");

	// Wait for the hand tracker to be totally done with the current frame, then make it wait trying to relock this
	// mutex for us to be done.
	os_mutex_lock(&htd->unlocked_between_frames);

	if (htd->tracking_should_record_dataset == false) {
		// Then we're starting up the pipeline.
		HT_INFO(htd, "Starting dataset recording!");


		const char *source_name = "source_name";
		char pipeline_string[2048];

		/*
		None (0) – No preset
		ultrafast (1) – ultrafast
		superfast (2) – superfast
		veryfast (3) – veryfast
		faster (4) – faster
		fast (5) – fast
		medium (6) – medium
		slow (7) – slow
		slower (8) – slower
		veryslow (9) – veryslow
		placebo (10) – placebo
		*/

#if 0
		snprintf(pipeline_string,         //
		         sizeof(pipeline_string), //
		         "appsrc name=\"%s\" ! "
		         "queue ! "
		         "videoconvert ! "
		         "queue ! "
		         "x264enc pass=qual quantizer=0 tune=film bitrate=\"%s\" speed-preset=\"%s\" ! "
		         "h264parse ! "
		         "queue ! "
		         "mp4mux ! "
		         "filesink location=\"%s\"",
		         source_name, "16384", "fast", "/tmp/moses.mp4");
#elif 1
		snprintf(pipeline_string,         //
		         sizeof(pipeline_string), //
		         "appsrc name=\"%s\" ! "
		         "queue ! "
		         "videoconvert ! "
		         "queue ! "
		         "x264enc pass=quant quantizer=20 tune=\"film\" speed-preset=\"veryfast\" ! "
		         "h264parse ! "
		         "queue ! "
		         "matroskamux ! "
		         "filesink location=\"%s\"",
		         source_name, "/tmp/moses.mkv");
#elif 1
		snprintf(pipeline_string,         //
		         sizeof(pipeline_string), //
		         "appsrc name=\"%s\" ! "
		         "queue ! "
		         "videoconvert ! "
		         "x265enc ! "
		         "h265parse ! "
		         "matroskamux ! "
		         "filesink location=\"%s\"",
		         source_name, "/tmp/moses.mkv");
#endif

		gstreamer_pipeline_create_from_string(&htd->gst.xfctx, pipeline_string, &htd->gst.gp);

		gstreamer_sink_create_with_pipeline(htd->gst.gp, 2560, 800, XRT_FORMAT_R8G8B8, source_name,
		                                    &htd->gst.gs, &htd->gst.sink);
		gstreamer_pipeline_play(htd->gst.gp);


		htd->gst.output_root = cJSON_CreateObject();
		htd->gst.output_array = cJSON_CreateArray();
		cJSON_AddItemToObject(htd->gst.output_root, "hand_array", htd->gst.output_array);

		strcpy(htd->gui.start_json_record.label, "Stop recording and save dataset!");
		htd->gst.current_index = 0;
		htd->tracking_should_record_dataset = true;

	} else {
		// Then the pipeline was created sometime in the past and we have to destroy it + save everything to a
		// file.

		gstreamer_pipeline_stop(htd->gst.gp);

		xrt_frame_context_destroy_nodes(&htd->gst.xfctx);


		cJSON_AddNumberToObject(htd->gst.output_root, "num_frames", htd->gst.current_index);
		cJSON_AddNumberToObject(htd->gst.output_root, "length_ns", htd->gst.last_frame_ns);
		const char *string = cJSON_Print(htd->gst.output_root);
		FILE *fp;
		fp = fopen("/tmp/moses.json", "w");
		fprintf(fp, "%s", string);
		fclose(fp);
		cJSON_Delete(htd->gst.output_root);

		strcpy(htd->gui.start_json_record.label, "Start recording dataset!");
		htd->tracking_should_record_dataset = false;
	}

	// We're done; let the hand tracker go about its business
	os_mutex_unlock(&htd->unlocked_between_frames);
}
#endif

static void
on_video_device(struct xrt_prober *xp,
                struct xrt_prober_device *pdev,
                const char *product,
                const char *manufacturer,
                const char *serial,
                void *ptr)
{
	// Stolen from gui_scene_record

	struct ht_device *htd = (struct ht_device *)ptr;

	// Hardcoded for the Index.
	if (product != NULL && manufacturer != NULL) {
		if ((strcmp(product, "3D Camera") == 0) && (strcmp(manufacturer, "Etron Technology, Inc.") == 0)) {
			xrt_prober_open_video_device(xp, pdev, &htd->camera.xfctx, &htd->camera.xfs);
			return;
		}
	}
}

/*!
 * xrt_frame_sink function implementations
 */

static void
ht_sink_push_frame(struct xrt_frame_sink *xs, struct xrt_frame *xf)
{
	XRT_TRACE_MARKER();
	struct ht_device *htd = container_of(xs, struct ht_device, sink);
	assert(xf != NULL);

	if (!htd->tracking_should_die) {
		os_mutex_lock(&htd->unlocked_between_frames);

		xrt_frame_reference(&htd->frame_for_process, xf);
		htRunAlgorithm(htd);
		xrt_frame_reference(&htd->frame_for_process, NULL); // Could let go of it a little earlier but nah

		os_mutex_unlock(&htd->unlocked_between_frames);
	}
}

/*!
 * xrt_frame_node function implementations
 */

static void
ht_node_break_apart(struct xrt_frame_node *node)
{
	struct ht_device *htd = container_of(node, struct ht_device, node);
	HT_DEBUG(htd, "called!");
	// wrong but don't care
}

static void
ht_node_destroy(struct xrt_frame_node *node)
{
	struct ht_device *htd = container_of(node, struct ht_device, node);

	HT_DEBUG(htd, "called!");
}

/*!
 * xrt_device function implementations
 */

static void
ht_device_update_inputs(struct xrt_device *xdev)
{
	// Empty
}

static void
ht_device_get_hand_tracking(struct xrt_device *xdev,
                            enum xrt_input_name name,
                            uint64_t at_timestamp_ns,
                            struct xrt_hand_joint_set *out_value,
                            uint64_t *out_timestamp_ns)
{
	struct ht_device *htd = ht_device(xdev);

	if (name != XRT_INPUT_GENERIC_HAND_TRACKING_LEFT && name != XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
		HT_ERROR(htd, "unknown input name for hand tracker");
		return;
	}
	bool hand_index = (name == XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT); // left=0, right=1



	os_mutex_lock(&htd->openxr_hand_data_mediator);
	memcpy(out_value, &htd->hands_for_openxr[hand_index], sizeof(struct xrt_hand_joint_set));
	// Instead of pose-predicting, we tell the caller that this joint set is a little old
	*out_timestamp_ns = htd->hands_for_openxr_timestamp;
	os_mutex_unlock(&htd->openxr_hand_data_mediator);
}

static void
ht_device_destroy(struct xrt_device *xdev)
{
	struct ht_device *htd = ht_device(xdev);
	HT_DEBUG(htd, "called!");


	xrt_frame_context_destroy_nodes(&htd->camera.xfctx);
#ifdef EXPERIMENTAL_DATASET_RECORDING
	xrt_frame_context_destroy_nodes(&htd->gst.xfctx);
#endif
	htd->tracking_should_die = true;

	// Lock this mutex so we don't try to free things as they're being used on the last iteration
	os_mutex_lock(&htd->unlocked_between_frames);
	destroyOnnx(htd);
	// Remove the variable tracking.
	u_var_remove_root(htd);

	// Shhhhhhhhhhh, it's okay. It'll all be okay.
	htd->histories_3d.~vector();
	htd->views[0].bbox_histories.~vector();
	htd->views[1].bbox_histories.~vector();
	// Okay, fine, since we're mixing C and C++ idioms here, I couldn't find a clean way to implicitly
	// call the destructors on these (ht_device doesn't have a destructor; neither do most of its members; and if
	// you read u_device_allocate and u_device_free you'll agree it'd be somewhat annoying to write a
	// constructor/destructor for ht_device), so we just manually call the destructors for things like std::vector's
	// that need their destructors to be called to not leak.

	delete htd->views[0].htm;
	delete htd->views[1].htm;

	u_device_free(&htd->base);
}

extern "C" struct xrt_device *
ht_device_create(struct xrt_prober *xp, struct t_stereo_camera_calibration *calib)
{
	enum ht_run_type run_type = HT_RUN_TYPE_VALVE_INDEX;
	XRT_TRACE_MARKER();
	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_NO_FLAGS;

	//! @todo 2 hands hardcoded
	int num_hands = 2;

	// Allocate device
	struct ht_device *htd = U_DEVICE_ALLOCATE(struct ht_device, flags, num_hands, 0);

	// Setup logging first. We like logging.
	htd->log_level = debug_get_log_option_ht_log();

	/*
	 * Get configuration
	 */

	assert(calib != NULL);
	htd->run_type = run_type;
	getCalibration(htd, calib);
	// Set defaults - most people won't have a config json and it won't get past here.
	userConfigSetDefaults(htd);
	getUserConfig(htd);
	getModelsFolder(htd);

	/*
	 * Add our xrt_frame_sink and xrt_frame_node implementations to ourselves
	 */

	htd->sink.push_frame = &ht_sink_push_frame;
	htd->node.break_apart = &ht_node_break_apart;
	htd->node.destroy = &ht_node_destroy;
	// Add ourselves to the frame context
	xrt_frame_context_add(&htd->camera.xfctx, &htd->node);



	htd->camera.prober = xp;
	htd->camera.xfs = NULL; // paranoia

	xrt_prober_list_video_devices(htd->camera.prober, on_video_device, htd);

	if (htd->camera.xfs == NULL) {
		return NULL;
	}


	htd->views[0].htd = htd;
	htd->views[1].htd = htd; // :)

	htd->views[0].htm = new ht_model(htd);
	htd->views[1].htm = new ht_model(htd);

	htd->views[0].view = 0;
	htd->views[1].view = 1;

	initOnnx(htd);

	htd->base.tracking_origin = &htd->tracking_origin;
	htd->base.tracking_origin->type = XRT_TRACKING_TYPE_RGB;
	htd->base.tracking_origin->offset.position.x = 0.0f;
	htd->base.tracking_origin->offset.position.y = 0.0f;
	htd->base.tracking_origin->offset.position.z = 0.0f;
	htd->base.tracking_origin->offset.orientation.w = 1.0f;

	os_mutex_init(&htd->openxr_hand_data_mediator);
	os_mutex_init(&htd->unlocked_between_frames);

	htd->base.update_inputs = ht_device_update_inputs;
	htd->base.get_hand_tracking = ht_device_get_hand_tracking;
	htd->base.destroy = ht_device_destroy;

	snprintf(htd->base.str, XRT_DEVICE_NAME_LEN, "Camera based Hand Tracker");
	snprintf(htd->base.serial, XRT_DEVICE_NAME_LEN, "Camera based Hand Tracker");

	htd->base.inputs[0].name = XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
	htd->base.inputs[1].name = XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;

	// Yes, you need all of these. Yes, I tried disabling them all one at a time. You need all of these.
	htd->base.name = XRT_DEVICE_HAND_TRACKER;
	htd->base.device_type = XRT_DEVICE_TYPE_HAND_TRACKER;
	htd->base.orientation_tracking_supported = true;
	htd->base.position_tracking_supported = true;
	htd->base.hand_tracking_supported = true;

	struct xrt_frame_sink *tmp = &htd->sink;


	// This puts u_sink_create_to_r8g8b8_or_l8 on its own thread, so that nothing gets backed up if it runs slower
	// than the native camera framerate.
	u_sink_queue_create(&htd->camera.xfctx, tmp, &tmp);

	// Converts images (we'd expect YUV422 or MJPEG) to R8G8B8. Can take a long time, especially on unoptimized
	// builds. If it's really slow, triple-check that you built Monado with optimizations!
	u_sink_create_format_converter(&htd->camera.xfctx, XRT_FORMAT_R8G8B8, tmp, &tmp);

	// Puts the hand tracking code on its own thread, so that nothing upstream of it gets backed up if the hand
	// tracking code runs slower than the upstream framerate.
	u_sink_queue_create(&htd->camera.xfctx, tmp, &tmp);

	xrt_fs_mode *modes;
	uint32_t count;

	xrt_fs_enumerate_modes(htd->camera.xfs, &modes, &count);

	// Index should only have XRT_FORMAT_YUYV422 or XRT_FORMAT_MJPEG.

	bool found_mode = false;
	uint32_t selected_mode = 0;

	for (; selected_mode < count; selected_mode++) {
		if (modes[selected_mode].format == htd->startup_config.desired_format) {
			found_mode = true;
			break;
		}
	}

	if (!found_mode) {
		selected_mode = 0;
		HT_WARN(htd, "Couldn't find desired camera mode! Something's probably wrong.");
	}

	free(modes);

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

#ifdef EXPERIMENTAL_DATASET_RECORDING
	htd->gui.start_json_record.ptr = htd;
	htd->gui.start_json_record.cb = htStartJsonCB;
	strcpy(htd->gui.start_json_record.label, "Start recording dataset!");
	u_var_add_button(htd, &htd->gui.start_json_record, "");
#endif

	u_var_add_sink_debug(htd, &htd->debug_sink, "i");

	xrt_fs_stream_start(htd->camera.xfs, tmp, XRT_FS_CAPTURE_TYPE_TRACKING, selected_mode);

	HT_DEBUG(htd, "Hand Tracker initialized!");


	return &htd->base;
}
