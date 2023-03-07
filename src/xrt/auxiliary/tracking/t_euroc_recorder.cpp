// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  EuRoC dataset recorder utility.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_tracking
 */

#include "t_euroc_recorder.h"

#include "os/os_time.h"
#include "util/u_frame.h"
#include "util/u_sink.h"
#include "util/u_var.h"
#include "util/u_debug.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_tracking.h"

#include <cassert>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <queue>
#include <iomanip>

#include <opencv2/imgcodecs.hpp>

DEBUG_GET_ONCE_BOOL_OPTION(euroc_recorder_use_jpg, "EUROC_RECORDER_USE_JPG", false)

using std::lock_guard;
using std::mutex;
using std::ofstream;
using std::queue;
using std::string;
using std::to_string;
using std::vector;
using std::filesystem::create_directories;

struct euroc_recorder
{
	struct xrt_frame_node node;
	string path; //!< Destination path for the dataset
	int cam_count = -1;

	bool recording;                    //!< Whether samples are being recorded
	bool files_created;                //!< Whether the dataset directory structure has been created
	struct u_var_button recording_btn; //!< UI button to start/stop `recording`

	bool use_jpg; //! Whether or not we should save images as .jpg files

	// Cloner sinks: copy frame to heap for quick release of the original
	struct xrt_slam_sinks cloner_queues; //!< Queue sinks that write into cloner sinks
	struct xrt_imu_sink cloner_imu_sink;
	struct xrt_pose_sink cloner_gt_sink;
	struct xrt_frame_sink cloner_sinks[XRT_TRACKING_MAX_SLAM_CAMS];

	// Writer sinks: write copied frame to disk
	struct xrt_slam_sinks writer_queues; //!< Queue sinks that write into writer sinks
	struct xrt_imu_sink writer_imu_sink;
	struct xrt_pose_sink writer_gt_sink;
	struct xrt_frame_sink writer_sinks[XRT_TRACKING_MAX_SLAM_CAMS];

	queue<xrt_imu_sample> imu_queue{}; //!< IMU pushes get saved here and are delayed until left_frame pushes
	mutex imu_queue_lock{};            //!< Lock for imu_queue

	queue<xrt_pose_sample> gt_queue{}; //!< GT pushes get saved here and are delayed until left_frame pushes
	mutex gt_queue_lock{};             //!< Lock for gt_queue

	// CSV file handles, ofstream implementation is already buffered.
	// Using pointers because of `container_of`
	ofstream *imu_csv = nullptr;
	ofstream *gt_csv = nullptr;
	ofstream *cams_csv[XRT_TRACKING_MAX_SLAM_CAMS] = {};
};


/*
 *
 * Writer sinks functionality
 *
 */

static void
euroc_recorder_try_mkfiles(struct euroc_recorder *er)
{
	// Create directory structure and files only once
	if (er->files_created) {
		return;
	}
	er->files_created = true;

	string path = er->path;

	create_directories(path + "/mav0/imu0");
	er->imu_csv = new ofstream{path + "/mav0/imu0/data.csv"};
	*er->imu_csv << std::fixed << std::setprecision(CSV_PRECISION);
	*er->imu_csv << "#timestamp [ns],w_RS_S_x [rad s^-1],w_RS_S_y [rad s^-1],w_RS_S_z [rad s^-1],"
	                "a_RS_S_x [m s^-2],a_RS_S_y [m s^-2],a_RS_S_z [m s^-2]" CSV_EOL;

	create_directories(path + "/mav0/gt");
	er->gt_csv = new ofstream{path + "/mav0/gt/data.csv"};
	*er->gt_csv << std::fixed << std::setprecision(CSV_PRECISION);
	*er->gt_csv << "#timestamp [ns],p_RS_R_x [m],p_RS_R_y [m],p_RS_R_z [m],"
	               "q_RS_w [],q_RS_x [],q_RS_y [],q_RS_z []" CSV_EOL;

	for (int i = 0; i < er->cam_count; i++) {
		string data_path = path + "/mav0/cam" + to_string(i) + "/data";
		create_directories(data_path);
		er->cams_csv[i] = new ofstream{data_path + ".csv"};
		*er->cams_csv[i] << "#timestamp [ns],filename" CSV_EOL;
	}
}

static void
euroc_recorder_flush(struct euroc_recorder *er)
{
	// Flush IMU samples
	vector<xrt_imu_sample> imu_samples;

	{ // Move samples out of imu_queue into vector to minimize mutex contention
		lock_guard lock{er->imu_queue_lock};
		imu_samples.reserve(er->imu_queue.size());
		while (!er->imu_queue.empty()) {
			imu_samples.push_back(er->imu_queue.front());
			er->imu_queue.pop();
		}
	}

	// Write queued IMU samples to csv stream.
	for (xrt_imu_sample &sample : imu_samples) {
		xrt_sink_push_imu(&er->writer_imu_sink, &sample);
	}

	// Flush groundtruth samples
	vector<xrt_pose_sample> gt_samples;

	{ // Move samples out of gt_queue into vector to minimize mutex contention
		lock_guard lock{er->gt_queue_lock};
		gt_samples.reserve(er->gt_queue.size());
		while (!er->gt_queue.empty()) {
			gt_samples.push_back(er->gt_queue.front());
			er->gt_queue.pop();
		}
	}

	// Write queued gt samples to csv stream.
	for (xrt_pose_sample &sample : gt_samples) {
		xrt_sink_push_pose(&er->writer_gt_sink, &sample);
	}

	// Flush csv streams. Not necessary, doing it only to increase flush frequency
	er->imu_csv->flush();
	er->gt_csv->flush();
	for (int i = 0; i < er->cam_count; i++) {
		er->cams_csv[i]->flush();
	}
}

extern "C" void
euroc_recorder_save_imu(xrt_imu_sink *sink, struct xrt_imu_sample *sample)
{
	euroc_recorder *er = container_of(sink, euroc_recorder, writer_imu_sink);

	timepoint_ns ts = sample->timestamp_ns;
	xrt_vec3_f64 a = sample->accel_m_s2;
	xrt_vec3_f64 w = sample->gyro_rad_secs;

	*er->imu_csv << ts << ",";
	*er->imu_csv << w.x << "," << w.y << "," << w.z << ",";
	*er->imu_csv << a.x << "," << a.y << "," << a.z << CSV_EOL;
}

extern "C" void
euroc_recorder_save_gt(xrt_pose_sink *sink, struct xrt_pose_sample *sample)
{
	euroc_recorder *er = container_of(sink, euroc_recorder, writer_gt_sink);

	timepoint_ns ts = sample->timestamp_ns;
	xrt_vec3 p = sample->pose.position;
	xrt_quat o = sample->pose.orientation;

	*er->gt_csv << ts << ",";
	*er->gt_csv << p.x << "," << p.y << "," << p.z << ",";
	*er->gt_csv << o.w << "," << o.x << "," << o.y << "," << o.z << CSV_EOL;
}

static void
euroc_recorder_save_frame(euroc_recorder *er, struct xrt_frame *frame, int cam_index)
{
	string cam_name = "cam" + to_string(cam_index);
	uint64_t ts = frame->timestamp;

	assert(frame->format == XRT_FORMAT_L8 || frame->format == XRT_FORMAT_R8G8B8); // Only formats supported
	auto img_type = frame->format == XRT_FORMAT_L8 ? CV_8UC1 : CV_8UC3;
	string file_extension = er->use_jpg ? ".jpg" : ".png";
	string filename = std::to_string(ts) + file_extension;
	string img_path = er->path + "/mav0/" + cam_name + "/data/" + filename;
	cv::Mat img{(int)frame->height, (int)frame->width, img_type, frame->data, frame->stride};
	cv::imwrite(img_path, img);

	*er->cams_csv[cam_index] << ts << "," << filename << CSV_EOL;
}

#define DEFINE_SAVE_CAM(cam_id)                                                                                        \
	extern "C" void euroc_recorder_save_cam##cam_id(struct xrt_frame_sink *sink, struct xrt_frame *frame)          \
	{                                                                                                              \
		euroc_recorder *er = container_of(sink, euroc_recorder, writer_sinks[cam_id]);                         \
		if (cam_id == 0) {                                                                                     \
			euroc_recorder_flush(er);                                                                      \
		}                                                                                                      \
		euroc_recorder_save_frame(er, frame, cam_id);                                                          \
	}

DEFINE_SAVE_CAM(0)
DEFINE_SAVE_CAM(1)
DEFINE_SAVE_CAM(2)
DEFINE_SAVE_CAM(3)
DEFINE_SAVE_CAM(4)

//! Be sure to define the same number of defined functions as
//! XRT_TRACKING_MAX_SLAM_CAMS and to add them to to euroc_recorder_save_cam
static void (*euroc_recorder_save_cam[XRT_TRACKING_MAX_SLAM_CAMS])(struct xrt_frame_sink *sink,
                                                                   struct xrt_frame *frame) = {
    euroc_recorder_save_cam0, //
    euroc_recorder_save_cam1, //
    euroc_recorder_save_cam2, //
    euroc_recorder_save_cam3, //
    euroc_recorder_save_cam4, //
};


/*
 *
 * Cloner sinks functionality
 *
 */

extern "C" void
euroc_recorder_receive_imu(xrt_imu_sink *sink, struct xrt_imu_sample *sample)
{
	// Contrary to frame sinks, we don't have separately threaded queues for IMU
	// sinks so we use an std::queue to temporarily store IMU samples, later we
	// write them to disk when writing left frames.
	euroc_recorder *er = container_of(sink, euroc_recorder, cloner_imu_sink);

	if (!er->recording) {
		return;
	}

	{
		lock_guard lock{er->imu_queue_lock};
		er->imu_queue.push(*sample);
	}
}

extern "C" void
euroc_recorder_receive_gt(xrt_pose_sink *sink, struct xrt_pose_sample *sample)
{
	// This works similarly to euroc_recorder_receive_imu, read its comments
	euroc_recorder *er = container_of(sink, euroc_recorder, cloner_gt_sink);

	if (!er->recording) {
		return;
	}

	{
		lock_guard lock{er->gt_queue_lock};
		er->gt_queue.push(*sample);
	}
}


static void
euroc_recorder_receive_frame(euroc_recorder *er, struct xrt_frame *src_frame, int cam_index)
{
	if (!er->recording) {
		return;
	}

	// Let's clone the frame so that we can release the src_frame quickly
	xrt_frame *copy = nullptr;
	u_frame_clone(src_frame, &copy);

	xrt_sink_push_frame(er->writer_queues.cams[cam_index], copy);

	xrt_frame_reference(&copy, NULL);
}

#define DEFINE_RECEIVE_CAM(cam_id)                                                                                     \
	extern "C" void euroc_recorder_receive_cam##cam_id(struct xrt_frame_sink *sink, struct xrt_frame *frame)       \
	{                                                                                                              \
		euroc_recorder *er = container_of(sink, euroc_recorder, cloner_sinks[cam_id]);                         \
		euroc_recorder_receive_frame(er, frame, cam_id);                                                       \
	}

DEFINE_RECEIVE_CAM(0)
DEFINE_RECEIVE_CAM(1)
DEFINE_RECEIVE_CAM(2)
DEFINE_RECEIVE_CAM(3)
DEFINE_RECEIVE_CAM(4)

//! Be sure to define the same number of defined functions as
//! XRT_TRACKING_MAX_SLAM_CAMS and to add them to to euroc_recorder_receive_cam
static void (*euroc_recorder_receive_cam[XRT_TRACKING_MAX_SLAM_CAMS])(struct xrt_frame_sink *,
                                                                      struct xrt_frame *) = {
    euroc_recorder_receive_cam0, //
    euroc_recorder_receive_cam1, //
    euroc_recorder_receive_cam2, //
    euroc_recorder_receive_cam3, //
    euroc_recorder_receive_cam4, //
};


/*
 *
 * Frame node functionality
 *
 */

extern "C" void
euroc_recorder_node_break_apart(struct xrt_frame_node *node)
{}

extern "C" void
euroc_recorder_node_destroy(struct xrt_frame_node *node)
{
	struct euroc_recorder *er = container_of(node, struct euroc_recorder, node);
	delete er->imu_csv;
	delete er->gt_csv;
	for (int i = 0; i < er->cam_count; i++) {
		delete er->cams_csv[i];
	}
	delete er;
}


/*
 *
 * Exported functions
 *
 */

extern "C" xrt_slam_sinks *
euroc_recorder_create(struct xrt_frame_context *xfctx, const char *record_path, int cam_count, bool record_from_start)
{
	struct euroc_recorder *er = new euroc_recorder{};

	er->recording = record_from_start;
	er->cam_count = cam_count;

	struct xrt_frame_node *xfn = &er->node;
	xfn->break_apart = euroc_recorder_node_break_apart;
	xfn->destroy = euroc_recorder_node_destroy;
	xrt_frame_context_add(xfctx, xfn);

	// Determine dataset path
	if (record_path != nullptr) {
		er->path = record_path;
	} else {
		time_t seconds = os_realtime_get_ns() / U_1_000_000_000;
		constexpr size_t size = sizeof("YYYYMMDDHHmmss");
		char datetime[size] = {0};
		(void)strftime(datetime, size, "%Y%m%d%H%M%S", localtime(&seconds));
		string default_path = string{"euroc_recording_"} + datetime;
		er->path = default_path;
	}

	if (record_from_start) {
		euroc_recorder_try_mkfiles(er);
	}

	er->use_jpg = debug_get_bool_option_euroc_recorder_use_jpg();

	// Setup sink pipeline

	// We expose a "cloner" sink that will clone frames in memory so that original
	// frames can be released as soon as possible. Not doing this could result in
	// frame queues from the user being filled up. After that we write to disk. We
	// also put queues between these to support sensors streaming on different
	// threads.
	// cloner_queue -> cloner_sink (clone ) -> writer_queue -> writer_sink (write to disk)

	er->cloner_queues.cam_count = er->cam_count;
	er->writer_queues.cam_count = er->cam_count;
	for (int i = 0; i < er->cam_count; i++) {

		// If any of these asserts failed see docs on euroc_recorder_receive/save_cam
		assert(euroc_recorder_receive_cam[ARRAY_SIZE(euroc_recorder_receive_cam) - 1] != nullptr);
		assert(euroc_recorder_save_cam[ARRAY_SIZE(euroc_recorder_save_cam) - 1] != nullptr);

		u_sink_queue_create(xfctx, 0, &er->cloner_sinks[i], &er->cloner_queues.cams[i]);
		er->cloner_sinks[i].push_frame = euroc_recorder_receive_cam[i];
		u_sink_queue_create(xfctx, 0, &er->writer_sinks[i], &er->writer_queues.cams[i]);
		er->writer_sinks[i].push_frame = euroc_recorder_save_cam[i];
	}

	er->cloner_queues.imu = &er->cloner_imu_sink;
	er->cloner_imu_sink.push_imu = euroc_recorder_receive_imu;
	er->writer_queues.imu = nullptr; // We use a std::queue instead
	er->writer_imu_sink.push_imu = euroc_recorder_save_imu;

	er->cloner_queues.gt = &er->cloner_gt_sink;
	er->cloner_gt_sink.push_pose = euroc_recorder_receive_gt;
	er->writer_queues.gt = nullptr; // We use a std::queue instead
	er->writer_gt_sink.push_pose = euroc_recorder_save_gt;

	xrt_slam_sinks *public_sinks = &er->cloner_queues;
	return public_sinks;
}

static void
euroc_recorder_btn_cb(void *ptr)
{
	euroc_recorder *er = (euroc_recorder *)ptr;
	euroc_recorder_try_mkfiles(er);
	er->recording = !er->recording;
	(void)snprintf(er->recording_btn.label, sizeof(er->recording_btn.label),
	               er->recording ? "Stop recording" : "Record EuRoC dataset");
}

extern "C" void
euroc_recorder_add_ui(struct xrt_slam_sinks *public_sinks, void *root, const char *prefix)
{
	euroc_recorder *er = container_of(public_sinks, euroc_recorder, cloner_queues);
	er->recording_btn.cb = euroc_recorder_btn_cb;
	er->recording_btn.ptr = er;

	char tmp[256];
	(void)snprintf(tmp, sizeof(tmp), "%s%s", prefix, er->recording ? "Stop recording" : "Record EuRoC dataset");
	u_var_add_button(root, &er->recording_btn, tmp);
}
