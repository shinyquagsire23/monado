// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PS Move tracker code.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_tracking
 */

#include "xrt/xrt_tracking.h"

#include "tracking/t_tracking.h"
#include "tracking/t_calibration_opencv.hpp"
#include "tracking/t_tracker_psmv_fusion.hpp"
#include "tracking/t_helper_debug_sink.hpp"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_frame.h"
#include "util/u_format.h"

#include "math/m_api.h"

#include "os/os_threading.h"

#include <stdio.h>
#include <assert.h>
#include <pthread.h>


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

/*!
 * The core object of the PS Move tracking setup.
 *
 * @implements xrt_tracked_psmv
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct TrackerPSMV
{
	struct xrt_tracked_psmv base = {};
	struct xrt_frame_sink sink = {};
	struct xrt_frame_node node = {};

	//! Frame waiting to be processed.
	struct xrt_frame *frame;

	//! Thread and lock helper.
	struct os_thread_helper oth;

	bool tracked = false;

	HelperDebugSink debug = {HelperDebugSink::AllAvailable};

	//! Have we received a new IMU sample.
	bool has_imu = false;

	struct
	{
		struct xrt_vec3 pos = {};
		struct xrt_quat rot = {};
	} fusion;

	View view[2];

	bool calibrated;

	cv::Mat disparity_to_depth;
	cv::Vec3d r_cam_translation;
	cv::Matx33d r_cam_rotation;

	cv::Ptr<cv::SimpleBlobDetector> sbd;

	std::unique_ptr<xrt_fusion::PSMVFusionInterface> filter;

	xrt_vec3 tracked_object_position;
};


/*!
 * @brief Perform per-view (two in a stereo camera image) processing on an
 * image, before tracking math is performed.
 *
 * Right now, this is mainly finding blobs/keypoints.
 */
static void
do_view(TrackerPSMV &t, View &view, cv::Mat &grey, cv::Mat &rgb)
{
	// Undistort and rectify the whole image.
	cv::remap(grey,                         // src
	          view.frame_undist_rectified,  // dst
	          view.undistort_rectify_map_x, // map1
	          view.undistort_rectify_map_y, // map2
	          cv::INTER_NEAREST,            // interpolation
	          cv::BORDER_CONSTANT,          // borderMode
	          cv::Scalar(0, 0, 0));         // borderValue

	cv::threshold(view.frame_undist_rectified, // src
	              view.frame_undist_rectified, // dst
	              32.0,                        // thresh
	              255.0,                       // maxval
	              0);                          // type

	// tracker_measurement_t m = {};

	// Do blob detection with our masks.
	//! @todo Re-enable masks.
	t.sbd->detect(view.frame_undist_rectified, // image
	              view.keypoints,              // keypoints
	              cv::noArray());              // mask


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
 * @brief Helper struct that keeps the value that produces the lowest "score" as
 * computed by your functor.
 *
 * Having this as a struct with a method, instead of a single "algorithm"-style
 * function, allows you to keep your complicated filtering logic in your own
 * loop, just calling in when you have a new candidate for "best".
 *
 * @note Create by calling make_lowest_score_finder() with your
 * function/lambda that takes an element and returns the score, to deduce the
 * un-spellable typename of the lambda.
 *
 * @tparam ValueType The type of a single element value - whatever you want to
 * assign a score to.
 * @tparam FunctionType The type of your functor/lambda that turns a ValueType
 * into a float "score". Usually deduced.
 */
template <typename ValueType, typename FunctionType> struct FindLowestScore
{
	const FunctionType score_functor;
	bool got_one{false};
	ValueType best{};
	float best_score{0};

	void
	handle_candidate(ValueType val)
	{
		float score = score_functor(val);
		if (!got_one || score < best_score) {
			best = val;
			best_score = score;
			got_one = true;
		}
	}
};


//! Factory function for FindLowestScore to deduce the functor type.
template <typename ValueType, typename FunctionType>
static FindLowestScore<ValueType, FunctionType>
make_lowest_score_finder(FunctionType scoreFunctor)
{
	return FindLowestScore<ValueType, FunctionType>{scoreFunctor};
}

//! Convert our 2d point + disparities into 3d points.
static cv::Point3f
world_point_from_blobs(cv::Point2f left, cv::Point2f right, const cv::Matx44d &disparity_to_depth)
{
	float disp = right.x - left.x;
	cv::Vec4d xydw(left.x, left.y, disp, 1.0f);
	// Transform
	cv::Vec4d h_world = disparity_to_depth * xydw;

	// Divide by scale to get 3D vector from homogeneous coordinate.  we
	// also invert x here
	cv::Point3f world_point(-h_world[0] / h_world[3], h_world[1] / h_world[3], h_world[2] / h_world[3]);

	return world_point;
}

/*!
 * @brief Perform tracking computations on a frame of video data.
 */
static void
process(TrackerPSMV &t, struct xrt_frame *xf)
{
	// Only IMU data: nothing to do
	if (xf == NULL) {
		return;
	}

	// Wrong type of frame: unreference and return?
	if (xf->format != XRT_FORMAT_L8) {
		xrt_frame_reference(&xf, NULL);
		return;
	}

	if (!t.calibrated) {
		return;
	}

	// Create the debug frame if needed.
	t.debug.refresh(xf);

	t.view[0].keypoints.clear();
	t.view[1].keypoints.clear();

	int cols = xf->width / 2;
	int rows = xf->height;
	int stride = xf->stride;

	cv::Mat l_grey(rows, cols, CV_8UC1, xf->data, stride);
	cv::Mat r_grey(rows, cols, CV_8UC1, xf->data + cols, stride);

	do_view(t, t.view[0], l_grey, t.debug.rgb[0]);
	do_view(t, t.view[1], r_grey, t.debug.rgb[1]);

	cv::Point3f last_point(t.tracked_object_position.x, t.tracked_object_position.y, t.tracked_object_position.z);
	auto nearest_world = make_lowest_score_finder<cv::Point3f>([&](cv::Point3f world_point) {
		//! @todo don't really need the square root to be done here.
		return cv::norm(world_point - last_point);
	});
	// do some basic matching to come up with likely disparity-pairs.

	const cv::Matx44d disparity_to_depth = static_cast<cv::Matx44d>(t.disparity_to_depth);

	for (const cv::KeyPoint &l_keypoint : t.view[0].keypoints) {
		cv::Point2f l_blob = l_keypoint.pt;

		auto nearest_blob =
		    make_lowest_score_finder<cv::Point2f>([&](cv::Point2f r_blob) { return l_blob.x - r_blob.x; });

		for (const cv::KeyPoint &r_keypoint : t.view[1].keypoints) {
			cv::Point2f r_blob = r_keypoint.pt;
			// find closest point on same-ish scanline
			if ((l_blob.y < r_blob.y + 3) && (l_blob.y > r_blob.y - 3)) {
				nearest_blob.handle_candidate(r_blob);
			}
		}
		//! @todo do we need to avoid claiming the same counterpart
		//! several times?
		if (nearest_blob.got_one) {
			cv::Point3f pt = world_point_from_blobs(l_blob, nearest_blob.best, disparity_to_depth);
			nearest_world.handle_candidate(pt);
		}
	}

	if (nearest_world.got_one) {
		cv::Point3f world_point = nearest_world.best;
		// update internal state
		memcpy(&t.tracked_object_position, &world_point.x, sizeof(t.tracked_object_position));
	} else {
		t.filter->clear_position_tracked_flag();
	}

	// We are done with the debug frame.
	t.debug.submit();

	// We are done with the frame.
	xrt_frame_reference(&xf, NULL);

	if (nearest_world.got_one) {
#if 0
		//! @todo something less arbitrary for the lever arm?
		//! This puts the origin approximately under the PS
		//! button.
		xrt_vec3 lever_arm{0.f, 0.09f, 0.f};
		//! @todo this should depend on distance
		// Weirdly, this is where *not* applying the
		// disparity-to-distance/rectification/etc would
		// simplify things, since the measurement variance is
		// related to the image sensor. 1.e-4 means 1cm std dev.
		// Not sure how to estimate the depth variance without
		// some research.
		xrt_vec3 variance{1.e-4f, 1.e-4f, 4.e-4f};
#endif
		t.filter->process_3d_vision_data(0, &t.tracked_object_position, NULL, NULL,
		                                 //! @todo tune cutoff for residual arbitrarily "too large"
		                                 15);
	} else {
		t.filter->clear_position_tracked_flag();
	}
}

/*!
 * @brief Tracker processing thread function
 */
static void
run(TrackerPSMV &t)
{
	struct xrt_frame *frame = NULL;

	os_thread_helper_lock(&t.oth);

	while (os_thread_helper_is_running_locked(&t.oth)) {
		// No data
		if (!t.has_imu || t.frame == NULL) {
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

/*!
 * @brief Retrieves a pose from the filter.
 */
static void
get_pose(TrackerPSMV &t, enum xrt_input_name name, timepoint_ns when_ns, struct xrt_space_relation *out_relation)
{
	os_thread_helper_lock(&t.oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&t.oth)) {
		os_thread_helper_unlock(&t.oth);
		return;
	}

	if (name == XRT_INPUT_PSMV_BALL_CENTER_POSE) {
		out_relation->pose.position = t.tracked_object_position;
		out_relation->pose.orientation.x = 0.0f;
		out_relation->pose.orientation.y = 0.0f;
		out_relation->pose.orientation.z = 0.0f;
		out_relation->pose.orientation.w = 1.0f;

		out_relation->relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_POSITION_VALID_BIT |
		                                                               XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

		os_thread_helper_unlock(&t.oth);
		return;
	}

	t.filter->get_prediction(when_ns, out_relation);

	os_thread_helper_unlock(&t.oth);
}

static void
imu_data(TrackerPSMV &t, timepoint_ns timestamp_ns, struct xrt_tracking_sample *sample)
{
	os_thread_helper_lock(&t.oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&t.oth)) {
		os_thread_helper_unlock(&t.oth);
		return;
	}
	t.filter->process_imu_data(timestamp_ns, sample, NULL);

	os_thread_helper_unlock(&t.oth);
}

static void
frame(TrackerPSMV &t, struct xrt_frame *xf)
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

static void
break_apart(TrackerPSMV &t)
{
	os_thread_helper_stop(&t.oth);
}


/*
 *
 * C wrapper functions.
 *
 */

extern "C" void
t_psmv_push_imu(struct xrt_tracked_psmv *xtmv, timepoint_ns timestamp_ns, struct xrt_tracking_sample *sample)
{
	auto &t = *container_of(xtmv, TrackerPSMV, base);
	imu_data(t, timestamp_ns, sample);
}

extern "C" void
t_psmv_get_tracked_pose(struct xrt_tracked_psmv *xtmv,
                        enum xrt_input_name name,
                        timepoint_ns when_ns,
                        struct xrt_space_relation *out_relation)
{
	auto &t = *container_of(xtmv, TrackerPSMV, base);
	get_pose(t, name, when_ns, out_relation);
}

extern "C" void
t_psmv_fake_destroy(struct xrt_tracked_psmv *xtmv)
{
	auto &t = *container_of(xtmv, TrackerPSMV, base);
	(void)t;
	// Not the real destroy function
}

extern "C" void
t_psmv_sink_push_frame(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	auto &t = *container_of(xsink, TrackerPSMV, sink);
	frame(t, xf);
}

extern "C" void
t_psmv_node_break_apart(struct xrt_frame_node *node)
{
	auto &t = *container_of(node, TrackerPSMV, node);
	break_apart(t);
}

extern "C" void
t_psmv_node_destroy(struct xrt_frame_node *node)
{
	auto t_ptr = container_of(node, TrackerPSMV, node);
	os_thread_helper_destroy(&t_ptr->oth);

	// Tidy variable setup.
	u_var_remove_root(t_ptr);

	delete t_ptr;
}

extern "C" void *
t_psmv_run(void *ptr)
{
	auto &t = *(TrackerPSMV *)ptr;
	run(t);
	return NULL;
}


/*
 *
 * Exported functions.
 *
 */

extern "C" int
t_psmv_start(struct xrt_tracked_psmv *xtmv)
{
	auto &t = *container_of(xtmv, TrackerPSMV, base);
	return os_thread_helper_start(&t.oth, t_psmv_run, &t);
}

extern "C" int
t_psmv_create(struct xrt_frame_context *xfctx,
              struct xrt_colour_rgb_f32 *rgb,
              struct t_stereo_camera_calibration *data,
              struct xrt_tracked_psmv **out_xtmv,
              struct xrt_frame_sink **out_sink)
{
	U_LOG_D("Creating PSMV tracker.");

	auto &t = *(new TrackerPSMV());
	int ret;

	t.base.get_tracked_pose = t_psmv_get_tracked_pose;
	t.base.push_imu = t_psmv_push_imu;
	t.base.destroy = t_psmv_fake_destroy;
	t.base.colour = *rgb;
	t.sink.push_frame = t_psmv_sink_push_frame;
	t.node.break_apart = t_psmv_node_break_apart;
	t.node.destroy = t_psmv_node_destroy;
	t.fusion.rot.x = 0.0f;
	t.fusion.rot.y = 0.0f;
	t.fusion.rot.z = 0.0f;
	t.fusion.rot.w = 1.0f;
	t.filter = xrt_fusion::PSMVFusionInterface::create();

	ret = os_thread_helper_init(&t.oth);
	if (ret != 0) {
		delete (&t);
		return ret;
	}

	static int hack = 0;
	switch (hack++) {
	case 0:
		t.fusion.pos.x = -0.3f;
		t.fusion.pos.y = 1.3f;
		t.fusion.pos.z = -0.5f;
		break;
	case 1:
		t.fusion.pos.x = 0.3f;
		t.fusion.pos.y = 1.3f;
		t.fusion.pos.z = -0.5f;
		break;
	default:
		t.fusion.pos.x = 0.0f;
		t.fusion.pos.y = 0.8f + hack * 0.1f;
		t.fusion.pos.z = -0.5f;
		break;
	}

	StereoRectificationMaps rectify(data);
	t.view[0].populate_from_calib(data->view[0], rectify.view[0].rectify);
	t.view[1].populate_from_calib(data->view[1], rectify.view[1].rectify);
	t.disparity_to_depth = rectify.disparity_to_depth_mat;
	StereoCameraCalibrationWrapper wrapped(data);
	t.r_cam_rotation = wrapped.camera_rotation_mat;
	t.r_cam_translation = wrapped.camera_translation_mat;
	t.calibrated = true;

	// clang-format off
	cv::SimpleBlobDetector::Params blob_params;
	blob_params.filterByArea = false;
	blob_params.filterByConvexity = true;
	blob_params.minConvexity = 0.8;
	blob_params.filterByInertia = false;
	blob_params.filterByColor = true;
	blob_params.blobColor = 255; // 0 or 255 - color comes from binarized image?
	blob_params.minArea = 1;
	blob_params.maxArea = 1000;
	blob_params.maxThreshold = 51; // using a wide threshold span slows things down bigtime
	blob_params.minThreshold = 50;
	blob_params.thresholdStep = 1;
	blob_params.minDistBetweenBlobs = 5;
	blob_params.minRepeatability = 1; // need this to avoid error?
	// clang-format on

	t.sbd = cv::SimpleBlobDetector::create(blob_params);
	xrt_frame_context_add(xfctx, &t.node);

	// Everything is safe, now setup the variable tracking.
	u_var_add_root(&t, "PSMV Tracker", true);
	u_var_add_vec3_f32(&t, &t.tracked_object_position, "last.ball.pos");
	u_var_add_sink(&t, &t.debug.sink, "Debug");

	*out_sink = &t.sink;
	*out_xtmv = &t.base;

	return 0;
}
