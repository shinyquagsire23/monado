// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PS Move tracker code.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_tracking.h"

#include "tracking/t_tracking.h"
#include "tracking/t_calibration_opencv.h"

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
 */
struct View
{
	cv::Mat undistort_map_x;
	cv::Mat undistort_map_y;
	cv::Mat rectify_map_x;
	cv::Mat rectify_map_y;

	std::vector<cv::KeyPoint> keypoints;

	cv::Mat frame_undist;
	cv::Mat frame_rectified;
};

class TrackerPSMV
{
public:
	struct xrt_tracked_psmv base = {};
	struct xrt_frame_sink sink = {};
	struct xrt_frame_node node = {};

	//! Frame waiting to be processed.
	struct xrt_frame *frame;

	//! Thread and lock helper.
	struct os_thread_helper oth;

	struct
	{
		struct xrt_frame_sink *sink;
		struct xrt_frame *frame;

		cv::Mat rgb[2];
	} debug;

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

	cv::Ptr<cv::SimpleBlobDetector> sbd;

	xrt_vec3 tracked_object_position;
};

static void
refresh_gui_frame(TrackerPSMV &t, struct xrt_frame *xf)
{
	if (t.debug.sink == NULL) {
		return;
	}

	// Also dereferences the old frame.
	u_frame_create_one_off(XRT_FORMAT_R8G8B8, xf->width, xf->height,
	                       &t.debug.frame);
	t.debug.frame->source_sequence = xf->source_sequence;

	int rows = xf->height;
	int cols = xf->width / 2;

	t.debug.rgb[0] = cv::Mat(rows,                   // rows
	                         cols,                   // cols
	                         CV_8UC3,                // channels
	                         t.debug.frame->data,    // data
	                         t.debug.frame->stride); // stride

	t.debug.rgb[1] = cv::Mat(rows,                           // rows
	                         cols,                           // cols
	                         CV_8UC3,                        // channels
	                         t.debug.frame->data + 3 * cols, // data
	                         t.debug.frame->stride);         // stride
}

static void
do_view(TrackerPSMV &t, View &view, cv::Mat &grey, cv::Mat &rgb)
{
	// Undistort the whole image.
	cv::remap(grey,                 // src
	          view.frame_undist,    // dst
	          view.undistort_map_x, // map1
	          view.undistort_map_y, // map2
	          cv::INTER_LINEAR,     // interpolation
	          cv::BORDER_CONSTANT,  // borderMode
	          cv::Scalar(0, 0, 0)); // borderValue

	// Rectify the whole image.
	cv::remap(view.frame_undist,    // src
	          view.frame_rectified, // dst
	          view.rectify_map_x,   // map1
	          view.rectify_map_y,   // map2
	          cv::INTER_LINEAR,     // interpolation
	          cv::BORDER_CONSTANT,  // borderMode
	          cv::Scalar(0, 0, 0)); // borderValue

	cv::threshold(view.frame_rectified, // src
	              view.frame_rectified, // dst
	              32.0,                 // thresh
	              255.0,                // maxval
	              0);                   // type

	// tracker_measurement_t m = {};

	// Do blob detection with our masks.
	//! @todo Re-enable masks.
	t.sbd->detect(view.frame_rectified, // image
	              view.keypoints,       // keypoints
	              cv::noArray());       // mask


	// Debug is wanted, draw the keypoints.
	if (rgb.cols > 0) {
		cv::drawKeypoints(
		    view.frame_rectified,                       // image
		    view.keypoints,                             // keypoints
		    rgb,                                        // outImage
		    cv::Scalar(255, 0, 0),                      // color
		    cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS); // flags
	}
}

static void
process(TrackerPSMV &t, struct xrt_frame *xf)
{
	// Only IMU data
	if (xf == NULL) {
		return;
	}

	if (xf->format != XRT_FORMAT_L8) {
		xrt_frame_reference(&xf, NULL);
		return;
	}

	if (!t.calibrated) {
		bool ok = calibration_get_stereo(
		    "PS4_EYE",                  // name
		    xf->width,                  // width
		    xf->height,                 // height
		    false,                      // use_fisheye
		    &t.view[0].undistort_map_x, // l_undistort_map_x
		    &t.view[0].undistort_map_y, // l_undistort_map_y
		    &t.view[0].rectify_map_x,   // l_rectify_map_x
		    &t.view[0].rectify_map_y,   // l_rectify_map_y
		    &t.view[1].undistort_map_x, // r_undistort_map_x
		    &t.view[1].undistort_map_y, // r_undistort_map_y
		    &t.view[1].rectify_map_x,   // r_rectify_map_x
		    &t.view[1].rectify_map_y,   // r_rectify_map_y
		    &t.disparity_to_depth);     // disparity_to_depth

		if (ok) {
			printf("loaded calibration for camera!\n");
			t.calibrated = true;
		} else {
			xrt_frame_reference(&xf, NULL);
			return;
		}
	}

	// Create the debug frame if needed.
	refresh_gui_frame(t, xf);

	t.view[0].keypoints.clear();
	t.view[1].keypoints.clear();

	int cols = xf->width / 2;
	int rows = xf->height;
	int stride = xf->stride;

	cv::Mat l_grey(rows, cols, CV_8UC1, xf->data, stride);
	cv::Mat r_grey(rows, cols, CV_8UC1, xf->data + cols, stride);

	do_view(t, t.view[0], l_grey, t.debug.rgb[0]);
	do_view(t, t.view[1], r_grey, t.debug.rgb[1]);

	// do some basic matching to come up with likely disparity-pairs.
	std::vector<cv::KeyPoint> l_blobs, r_blobs;
	for (uint32_t i = 0; i < t.view[0].keypoints.size(); i++) {
		cv::KeyPoint l_blob = t.view[0].keypoints[i];
		int l_index = -1;
		int r_index = -1;

		for (uint32_t j = 0; j < t.view[1].keypoints.size(); j++) {
			float lowest_dist = 128;
			cv::KeyPoint r_blob = t.view[1].keypoints[j];
			// find closest point on same-ish scanline
			if ((l_blob.pt.y < r_blob.pt.y + 3) &&
			    (l_blob.pt.y > r_blob.pt.y - 3) &&
			    ((r_blob.pt.x - l_blob.pt.x) < lowest_dist)) {
				lowest_dist = r_blob.pt.x - l_blob.pt.x;
				r_index = j;
				l_index = i;
			}
		}

		if (l_index > -1 && r_index > -1) {
			l_blobs.push_back(t.view[0].keypoints.at(l_index));
			r_blobs.push_back(t.view[1].keypoints.at(r_index));
		}
	}

	// Convert our 2d point + disparities into 3d points.
	std::vector<cv::Point3f> world_points;
	if (l_blobs.size() > 0) {
		for (uint32_t i = 0; i < l_blobs.size(); i++) {
			float disp = r_blobs[i].pt.x - l_blobs[i].pt.x;
			cv::Vec4d xydw(l_blobs[i].pt.x, l_blobs[i].pt.y, disp,
			               1.0f);
			// Transform
			cv::Vec4d h_world =
			    (cv::Matx44d)t.disparity_to_depth * xydw;

			// Divide by scale to get 3D vector from homogeneous
			// coordinate. invert x while we are here.
			world_points.push_back(cv::Point3f(
			    -h_world[0] / h_world[3], h_world[1] / h_world[3],
			    h_world[2] / h_world[3]));
		}
	}

	int tracked_index = -1;
	float lowest_dist = 65535.0f;

	cv::Point3f last_point(t.tracked_object_position.x,
	                       t.tracked_object_position.y,
	                       t.tracked_object_position.z);

	for (uint32_t i = 0; i < world_points.size(); i++) {
		float dist = cv_dist3d_point(world_points[i], last_point);
		if (dist < lowest_dist) {
			tracked_index = i;
			lowest_dist = dist;
		}
	}

	if (tracked_index != -1) {
		cv::Point3f world_point = world_points[tracked_index];

		/*
		//apply our room setup transform
		Eigen::Vector3f p = Eigen::Map<Eigen::Vector3f>(&world_point.x);
		Eigen::Vector4f pt;
		pt.x() = p.x();
		pt.y() = p.y();
		pt.z() = p.z();
		pt.w() = 1.0f;

		//this is a glm mat4 written out 'flat'
		Eigen::Matrix4f mat =
		Eigen::Map<Eigen::Matrix<float,4,4>>(internal->rs.origin_transform.v);
		pt = mat * pt;

		m.pose.position.x = pt.x();
		m.pose.position.y = pt.y();
		m.pose.position.z = pt.z();
		*/
		// update internal state

		t.tracked_object_position.x = world_point.x;
		t.tracked_object_position.y = world_point.y;
		t.tracked_object_position.z = world_point.z;
	}

	if (t.debug.frame != NULL) {
		t.debug.sink->push_frame(t.debug.sink, t.debug.frame);
		t.debug.rgb[0] = cv::Mat();
		t.debug.rgb[1] = cv::Mat();
	}

	xrt_frame_reference(&xf, NULL);
	xrt_frame_reference(&t.debug.frame, NULL);
}


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

static void
get_pose(TrackerPSMV &t,
         struct time_state *timestate,
         timepoint_ns when_ns,
         struct xrt_space_relation *out_relation)
{
	os_thread_helper_lock(&t.oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&t.oth)) {
		os_thread_helper_unlock(&t.oth);
		return;
	}

	// out_relation->pose.position = t.fusion.pos;
	out_relation->pose.position = t.tracked_object_position;
	out_relation->pose.orientation = t.fusion.rot;

	//! @todo assuming that orientation is actually currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_POSITION_TRACKED_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

	os_thread_helper_unlock(&t.oth);
}

static void
imu_data(TrackerPSMV &t,
         time_duration_ns delta_ns,
         struct xrt_tracking_sample *sample)
{
	os_thread_helper_lock(&t.oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&t.oth)) {
		os_thread_helper_unlock(&t.oth);
		return;
	}

	float dt = time_ns_to_s(delta_ns);
	// Super simple fusion.
	math_quat_integrate_velocity(&t.fusion.rot, &sample->gyro_rad_secs, dt,
	                             &t.fusion.rot);

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
t_psmv_push_imu(struct xrt_tracked_psmv *xtmv,
                time_duration_ns delta_ns,
                struct xrt_tracking_sample *sample)
{
	auto &t = *container_of(xtmv, TrackerPSMV, base);
	imu_data(t, delta_ns, sample);
}

extern "C" void
t_psmv_get_tracked_pose(struct xrt_tracked_psmv *xtmv,
                        struct time_state *timestate,
                        timepoint_ns when_ns,
                        struct xrt_space_relation *out_relation)
{
	auto &t = *container_of(xtmv, TrackerPSMV, base);
	get_pose(t, timestate, when_ns, out_relation);
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
              struct xrt_tracked_psmv **out_xtmv,
              struct xrt_frame_sink **out_sink)
{
	fprintf(stderr, "%s\n", __func__);

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

	// clang-format off
	cv::SimpleBlobDetector::Params blob_params;
	blob_params.filterByArea = false;
	blob_params.filterByConvexity = false;
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
	u_var_add_sink(&t, &t.debug.sink, "Debug");

	*out_sink = &t.sink;
	*out_xtmv = &t.base;

	return 0;
}
