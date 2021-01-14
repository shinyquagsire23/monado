// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Hand tracker code.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup aux_tracking
 */

#include "xrt/xrt_tracking.h"
#include "xrt/xrt_frame.h"

#include "os/os_threading.h"

#include "util/u_var.h"
#include "util/u_sink.h"

#include "t_helper_debug_sink.hpp"
#include "t_tracking.h"
#include "tracking/t_calibration_opencv.hpp"


/*!
 * Single camera.
 *
 * @see TrackerPSMV
 */
struct View
{
	cv::Mat undistort_rectify_map_x;
	cv::Mat undistort_rectify_map_y;

	cv::Matx33d intrinsics;
	cv::Mat distortion; // size may vary
	cv::Vec4d distortion_fisheye;
	bool use_fisheye;

	std::vector<cv::KeyPoint> keypoints;

	cv::Mat frame_undist_rectified;

	void
	populate_from_calib(t_camera_calibration &calib, const RemapPair &rectification)
	{
		CameraCalibrationWrapper wrap(calib);
		intrinsics = wrap.intrinsics_mat;
		distortion = wrap.distortion_mat.clone();
		distortion_fisheye = wrap.distortion_fisheye_mat;
		use_fisheye = wrap.use_fisheye;

		undistort_rectify_map_x = rectification.remap_x;
		undistort_rectify_map_y = rectification.remap_y;
	}
};

struct HandData
{
	//! the pose of the hand in space (e.g. palm)
	struct xrt_space_relation hand_relation;
	//! joint poses relative to hand_relation
	struct u_hand_joint_default_set joints;
};

/*!
 * The core object of the Hand tracking setup.
 *
 * @implements xrt_tracked_hand
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct TrackerHand
{
	struct xrt_tracked_hand base = {};
	struct xrt_frame_sink sink = {};
	struct xrt_frame_node node = {};

	//! Frame waiting to be processed.
	struct xrt_frame *frame;

	//! Thread and lock helper.
	struct os_thread_helper oth;

	bool tracked = false;

	HelperDebugSink debug = {HelperDebugSink::AllAvailable};


	View view[2];

	bool calibrated;

	cv::Mat disparity_to_depth;
	cv::Vec3d r_cam_translation;
	cv::Matx33d r_cam_rotation;

	// left, right
	struct HandData hand_data[2];
};

/*!
 * @brief Perform per-view (two in a stereo camera image) processing on an
 * image, before tracking math is performed.
 */
static void
do_view(TrackerHand &t, View &view, cv::Mat &grey, cv::Mat &rgb)
{
	// Undistort and rectify the whole image.
	//! @todo: This is an expensive operation, skip it if possible
	cv::remap(grey,                         // src
	          view.frame_undist_rectified,  // dst
	          view.undistort_rectify_map_x, // map1
	          view.undistort_rectify_map_y, // map2
	          cv::INTER_NEAREST,            // interpolation
	          cv::BORDER_CONSTANT,          // borderMode
	          cv::Scalar(0, 0, 0));         // borderValue

#if 0
	cv::threshold(view.frame_undist_rectified, // src
	              view.frame_undist_rectified, // dst
	              32.0,                        // thresh
	              255.0,                       // maxval
	              0);                          // type
#endif

	// Debug is wanted, draw the keypoints.
	if (rgb.cols > 0) {
		cv::drawKeypoints(view.frame_undist_rectified,                // image
		                  view.keypoints,                             // keypoints
		                  rgb,                                        // outImage
		                  cv::Scalar(255, 0, 0),                      // color
		                  cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS); // flags
	}
}

/*!
 * @brief Perform tracking computations on a frame of video data.
 */
static void
process(TrackerHand &t, struct xrt_frame *xf)
{
	if (xf == NULL) {
		return;
	}

	// Wrong type of frame: unreference and return?
	if (xf->format != XRT_FORMAT_R8G8B8) {
		xrt_frame_reference(&xf, NULL);
		return;
	}

	if (!t.calibrated) {
		return;
	}


	int cols = xf->width / 2;
	int rows = xf->height;
	int stride = xf->stride;

	int rect_cols = t.view[0].undistort_rectify_map_x.cols;
	int rect_rows = t.view[0].undistort_rectify_map_x.rows;

	if (cols != rect_cols || rows != rect_rows) {
		U_LOG_E("%dx%d rectification matrix does not fit %dx%d Image", rect_cols, rect_rows, cols, rows);
		return;
	}


	// Create the debug frame if needed.
	t.debug.refresh(xf);

	t.view[0].keypoints.clear();
	t.view[1].keypoints.clear();

#if 0
	cv::Mat l_grey(rows, cols, CV_8UC1, xf->data, stride);
	cv::Mat r_grey(rows, cols, CV_8UC1, xf->data + cols, stride);

	do_view(t, t.view[0], l_grey, t.debug.rgb[0]);
	do_view(t, t.view[1], r_grey, t.debug.rgb[1]);
	t.debug.submit();
#endif

#if 1
	cv::Mat l_rgb(rows, cols, CV_8UC3, xf->data, stride);
	cv::Mat r_rgb(rows, cols, CV_8UC3, xf->data + cols * 3, stride);

	do_view(t, t.view[0], l_rgb, t.debug.rgb[0]);
	do_view(t, t.view[1], r_rgb, t.debug.rgb[1]);
	t.debug.submit();
#endif

	//! @todo tracking

	// t.hand_data[i] =

	// We are done with the frame.
	xrt_frame_reference(&xf, NULL);
}

/*!
 * @brief Tracker processing thread function
 */
static void
run(TrackerHand &t)
{
	struct xrt_frame *frame = NULL;

	os_thread_helper_lock(&t.oth);

	while (os_thread_helper_is_running_locked(&t.oth)) {
		// No data
		if (t.frame == NULL) {
			os_thread_helper_wait_locked(&t.oth);
		}

		if (!os_thread_helper_is_running_locked(&t.oth)) {
			break;
		}

		// Take a reference on the current frame, this keeps it alive
		// if it is replaced during the consumer processing it, but
		// we no longer need to hold onto the frame on the queue we
		// just move the pointer.
		frame = t.frame;
		t.frame = NULL;

		// Unlock the mutex when we do the work.
		os_thread_helper_unlock(&t.oth);

		process(t, frame);

		// Have to lock it again.
		os_thread_helper_lock(&t.oth);
	}

	os_thread_helper_unlock(&t.oth);
}
extern "C" void *
t_ht_run(void *ptr)
{
	auto &t = *(TrackerHand *)ptr;
	run(t);
	return NULL;
}

extern "C" int
t_hand_start(struct xrt_tracked_hand *xth)
{
	auto &t = *container_of(xth, TrackerHand, base);
	return os_thread_helper_start(&t.oth, t_ht_run, &t);
}

static void
frame(TrackerHand &t, struct xrt_frame *xf)
{
	os_thread_helper_lock(&t.oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&t.oth)) {
		os_thread_helper_unlock(&t.oth);
		return;
	}

	xrt_frame_reference(&t.frame, xf);
	// Wake up the thread.
	os_thread_helper_signal_locked(&t.oth);

	os_thread_helper_unlock(&t.oth);
}

extern "C" void
t_hand_sink_push_frame(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	auto &t = *container_of(xsink, TrackerHand, sink);
	frame(t, xf);
}

extern "C" void
t_hand_node_break_apart(struct xrt_frame_node *node)
{
	auto &t = *container_of(node, TrackerHand, node);
	os_thread_helper_stop(&t.oth);
}

extern "C" void
t_hand_node_destroy(struct xrt_frame_node *node)
{
	auto t_ptr = container_of(node, TrackerHand, node);
	os_thread_helper_destroy(&t_ptr->oth);

	// Tidy variable setup.
	u_var_remove_root(t_ptr);

	delete t_ptr;
}

extern "C" void
t_hand_fake_destroy(struct xrt_tracked_hand *xth)
{
	auto &t = *container_of(xth, TrackerHand, base);
	(void)t;
	// Not the real destroy function
}

static void
get_joints(TrackerHand *t,
           int index,
           timepoint_ns when_ns,
           struct u_hand_joint_default_set *out_joints,
           struct xrt_space_relation *out_relation)
{
	//! @todo: prediction for when_ns
	*out_relation = t->hand_data[index].hand_relation;
	*out_joints = t->hand_data[index].joints;
}

extern "C" void
t_hand_get_tracked_joints(struct xrt_tracked_hand *xth,
                          enum xrt_input_name name,
                          timepoint_ns when_ns,
                          struct u_hand_joint_default_set *out_joints,
                          struct xrt_space_relation *out_relation)
{
	auto &t = *container_of(xth, TrackerHand, base);

	int index;
	if (name == XRT_INPUT_GENERIC_HAND_TRACKING_LEFT)
		index = 0;
	else if (name == XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
		index = 1;
	} else {
		return;
	}

	get_joints(&t, index, when_ns, out_joints, out_relation);
}

extern "C" int
t_hand_create(struct xrt_frame_context *xfctx,
              struct t_stereo_camera_calibration *data,
              struct xrt_tracked_hand **out_xth,
              struct xrt_frame_sink **out_sink)
{
	U_LOG_D("Creating hand tracker.");

	auto &t = *(new TrackerHand());
	int ret;

	t.base.get_tracked_joints = t_hand_get_tracked_joints;
	t.base.destroy = t_hand_fake_destroy;

	//! @todo format conversion to rgb hardcoded in p_tracker.c, see
	// u_sink_create_to_r8g8b8_or_l8
	t.sink.push_frame = t_hand_sink_push_frame;

	t.node.break_apart = t_hand_node_break_apart;
	t.node.destroy = t_hand_node_destroy;

	for (int i = 0; i < 2; i++) {
		t.hand_data[i].hand_relation.pose.orientation.x = 0;
		t.hand_data[i].hand_relation.pose.orientation.y = 0;
		t.hand_data[i].hand_relation.pose.orientation.z = 0;
		t.hand_data[i].hand_relation.pose.orientation.w = 1;

		t.hand_data[i].hand_relation.pose.position.x = 0;
		t.hand_data[i].hand_relation.pose.position.y = 0;
		t.hand_data[i].hand_relation.pose.position.z = 0;

		t.hand_data[i].hand_relation.angular_velocity.x = 0;
		t.hand_data[i].hand_relation.angular_velocity.y = 0;
		t.hand_data[i].hand_relation.angular_velocity.z = 0;

		t.hand_data[i].hand_relation.linear_velocity.x = 0;
		t.hand_data[i].hand_relation.linear_velocity.y = 0;
		t.hand_data[i].hand_relation.linear_velocity.z = 0;
	}

	ret = os_thread_helper_init(&t.oth);
	if (ret != 0) {
		delete (&t);
		return ret;
	}

	StereoRectificationMaps rectify(data);
	t.view[0].populate_from_calib(data->view[0], rectify.view[0].rectify);
	t.view[1].populate_from_calib(data->view[1], rectify.view[1].rectify);
	t.disparity_to_depth = rectify.disparity_to_depth_mat;
	StereoCameraCalibrationWrapper wrapped(data);
	t.r_cam_rotation = wrapped.camera_rotation_mat;
	t.r_cam_translation = wrapped.camera_translation_mat;
	t.calibrated = true;

	xrt_frame_context_add(xfctx, &t.node);

	// Everything is safe, now setup the variable tracking.
	u_var_add_root(&t, "Hand Tracker", true);
	u_var_add_vec3_f32(&t, &t.hand_data[0].hand_relation.pose.position, "hand.tracker.pos.0");
	u_var_add_vec3_f32(&t, &t.hand_data[1].hand_relation.pose.position, "hand.tracker.pos.1");
	u_var_add_sink(&t, &t.debug.sink, "Debug");

	*out_sink = &t.sink;
	*out_xth = &t.base;

	return 0;
}
