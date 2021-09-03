// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking driver code.
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ht
 */

#include "xrt/xrt_defines.h"
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

#include "templates/NaivePermutationSort.hpp"

#include "ht_driver.hpp"
#include "ht_algorithm.hpp"

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
	                  cv::Size(960, 960),                           // imageSize
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


	for (int i = 0; i < 2; i++) {
		htd->views[i].cameraMatrix = wrap.view[i].intrinsics_mat;

		htd->views[i].distortion = wrap.view[i].distortion_fisheye_mat;
	}

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
getUserConfig(struct ht_device *htd)
{
	// The game here is to avoid bugs + be paranoid, not to be fast. If you see something that seems "slow" - don't
	// fix it. Any of the tracking code is way stickier than this could ever be.

	// Set defaults
	// Admit defeat: for now, Mediapipe's are still better than ours.
	htd->runtime_config.palm_detection_use_mediapipe = true;
	htd->runtime_config.keypoint_estimation_use_mediapipe = true;

	// Make sure you build DebugOptimized!
	htd->runtime_config.desired_format = XRT_FORMAT_YUYV422;

	struct u_config_json config_json = {};

	u_config_json_open_or_create_main_file(&config_json);
	if (!config_json.file_loaded) {
		return;
	}

	cJSON *ht_config_json = cJSON_GetObjectItemCaseSensitive(config_json.root, "config_ht");
	if (ht_config_json == NULL) {
		return;
	}

	cJSON *palm_detection_type = cJSON_GetObjectItemCaseSensitive(ht_config_json, "palm_detection_model");
	cJSON *keypoint_estimation_type = cJSON_GetObjectItemCaseSensitive(ht_config_json, "keypoint_estimation_model");
	cJSON *uvc_wire_format = cJSON_GetObjectItemCaseSensitive(ht_config_json, "uvc_wire_format");

	// IsString does its own null-checking
	if (cJSON_IsString(palm_detection_type)) {
		bool is_collabora = (strcmp(palm_detection_type->valuestring, "collabora") == 0);
		bool is_mediapipe = (strcmp(palm_detection_type->valuestring, "mediapipe") == 0);
		if (!is_collabora && !is_mediapipe) {
			HT_WARN(htd, "Unknown palm detection type %s - should be \"collabora\" or \"mediapipe\"",
			        palm_detection_type->valuestring);
		}
		htd->runtime_config.palm_detection_use_mediapipe = is_mediapipe;
	}

	if (cJSON_IsString(keypoint_estimation_type)) {
		bool is_collabora = (strcmp(keypoint_estimation_type->valuestring, "collabora") == 0);
		bool is_mediapipe = (strcmp(keypoint_estimation_type->valuestring, "mediapipe") == 0);
		if (!is_collabora && !is_mediapipe) {
			HT_WARN(htd, "Unknown keypoint estimation type %s - should be \"collabora\" or \"mediapipe\"",
			        keypoint_estimation_type->valuestring);
		}
		htd->runtime_config.keypoint_estimation_use_mediapipe = is_mediapipe;
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
			htd->runtime_config.desired_format = XRT_FORMAT_YUYV422;
		} else {
			HT_DEBUG(htd, "Using MJPEG!");
			htd->runtime_config.desired_format = XRT_FORMAT_MJPEG;
		}
	}

	cJSON_Delete(config_json.root);
	return;
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
	strcpy(htd->runtime_config.model_slug, exec_location);
#else
	const char *xdg_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xdg_home != NULL) {
		strcpy(htd->runtime_config.model_slug, xdg_home);
	} else if (home != NULL) {
		strcpy(htd->runtime_config.model_slug, home);
	} else {
		assert(false);
	}
	strcat(htd->runtime_config.model_slug, "/.local/share/monado/hand-tracking-models/");
#endif
}

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
			htd->found_camera = true;
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
		os_mutex_lock(&htd->dying_breath);

		xrt_frame_reference(&htd->frame_for_process, xf);
		htRunAlgorithm(htd);
		xrt_frame_reference(&htd->frame_for_process, NULL); // Could let go of it a little earlier but nah

		os_mutex_unlock(&htd->dying_breath);
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
                            struct xrt_hand_joint_set *out_value)
{
	// Note! Currently, this totally ignores at_timestamp_ns. We need a better interface.
	struct ht_device *htd = ht_device(xdev);

	if (name != XRT_INPUT_GENERIC_HAND_TRACKING_LEFT && name != XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
		HT_ERROR(htd, "unknown input name for hand tracker");
		return;
	}
	bool hand_index = (name == XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT); // left=0, right=1



	os_mutex_lock(&htd->openxr_hand_data_mediator);
	memcpy(out_value, &htd->hands_for_openxr[hand_index], sizeof(struct xrt_hand_joint_set));
	os_mutex_unlock(&htd->openxr_hand_data_mediator);
}

static void
ht_device_destroy(struct xrt_device *xdev)
{
	struct ht_device *htd = ht_device(xdev);
	HT_DEBUG(htd, "called!");


	xrt_frame_context_destroy_nodes(&htd->camera.xfctx);
	htd->tracking_should_die = true;

	// Lock this mutex so we don't try to free things as they're being used on the last iteration
	os_mutex_lock(&htd->dying_breath);
	destroyOnnx(htd);
#if defined(JSON_OUTPUT)
	const char *string = cJSON_Print(htd->output_root);
	FILE *fp;
	fp = fopen("/1/2handtrack/aug12.json", "w");


	fprintf(fp, "%s", string);
	fclose(fp);
	cJSON_Delete(htd->output_root);
#endif

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

	u_device_free(&htd->base);
}

extern "C" struct xrt_device *
ht_device_create(struct xrt_prober *xp, struct t_stereo_camera_calibration *calib)
{
	XRT_TRACE_MARKER();
	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_NO_FLAGS;

	//! @todo 2 hands hardcoded
	int num_hands = 2;

	// Allocate device
	struct ht_device *htd = U_DEVICE_ALLOCATE(struct ht_device, flags, num_hands, 0);

	// Setup logging first. We like logging.
	htd->ll = debug_get_log_option_ht_log();

	// Get configuration
	assert(calib != NULL);
	getCalibration(htd, calib);
	getUserConfig(htd);
	getModelsFolder(htd);

	// Add xrt_frame_sink and xrt_frame_node implementations
	htd->sink.push_frame = &ht_sink_push_frame;
	htd->node.break_apart = &ht_node_break_apart;
	htd->node.destroy = &ht_node_destroy;

	// Add ourselves to the frame context
	xrt_frame_context_add(&htd->camera.xfctx, &htd->node);


	htd->camera.one_view_size_px.w = 960;
	htd->camera.one_view_size_px.h = 960;
	htd->camera.prober = xp;
	xrt_prober_list_video_devices(htd->camera.prober, on_video_device, htd);


	if (!htd->found_camera) {
		return NULL;
	}


	htd->views[0].htd = htd;
	htd->views[1].htd = htd; // :)

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
	os_mutex_init(&htd->dying_breath);

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

#if defined(JSON_OUTPUT)
	htd->output_root = cJSON_CreateObject();
	htd->output_array = cJSON_CreateArray();
	cJSON_AddItemToObject(htd->output_root, "hand_array", htd->output_array);
#endif

	struct xrt_frame_sink *tmp = &htd->sink;

	u_var_add_root(htd, "Camera based Hand Tracker", true);
	u_var_add_ro_text(htd, htd->base.str, "Name");

	// This puts u_sink_create_to_r8g8b8_or_l8 on its own thread, so that nothing gets backed up if it runs slower
	// than the native camera framerate.
	u_sink_queue_create(&htd->camera.xfctx, tmp, &tmp);

	// Converts images (we'd expect YUV422 or MJPEG) to R8G8B8. Can take a long time, especially on unoptimized
	// builds. If it's really slow, triple-check that you built Monado with optimizations!
	u_sink_create_to_r8g8b8_or_l8(&htd->camera.xfctx, tmp, &tmp);

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
		if (modes[selected_mode].format == htd->runtime_config.desired_format) {
			found_mode = true;
			break;
		}
	}

	if (!found_mode) {
		selected_mode = 0;
		HT_WARN(htd, "Couldn't find desired camera mode! Something's probably wrong.");
	}

	free(modes);


	xrt_fs_stream_start(htd->camera.xfs, tmp, XRT_FS_CAPTURE_TYPE_TRACKING, selected_mode);

#if 1
	u_var_add_sink(htd, &htd->debug_sink, "Debug visualization");
#endif


	HT_DEBUG(htd, "Hand Tracker initialized!");


	return &htd->base;
}
