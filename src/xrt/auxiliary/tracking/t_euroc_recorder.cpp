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

#include <cassert>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <queue>
#include <iomanip>

#include <opencv2/imgcodecs.hpp>

//! @todo: Now that IMU sinks support groundtruth, we could save it here as well.

using std::lock_guard;
using std::mutex;
using std::ofstream;
using std::queue;
using std::string;
using std::vector;
using std::filesystem::create_directories;

struct euroc_recorder
{
	struct xrt_frame_node node;
	string path; //!< Destination path for the dataset

	bool recording;                    //!< Whether samples are being recorded
	bool files_created;                //!< Whether the dataset directory structure has been created
	struct u_var_button recording_btn; //!< UI button to start/stop `recording`

	// Cloner sinks: copy frame to heap for quick release of the original
	struct xrt_slam_sinks cloner_queues; //!< Queue sinks that write into cloner sinks
	struct xrt_imu_sink cloner_imu_sink;
	struct xrt_frame_sink cloner_left_sink;
	struct xrt_frame_sink cloner_right_sink;

	// Writer sinks: write copied frame to disk
	struct xrt_slam_sinks writer_queues; //!< Queue sinks that write into writer sinks
	struct xrt_imu_sink writer_imu_sink;
	struct xrt_frame_sink writer_left_sink;
	struct xrt_frame_sink writer_right_sink;

	queue<xrt_imu_sample> imu_queue{}; //!< IMU pushes get saved here and are delayed until left_frame pushes
	mutex imu_queue_lock{};            //!< Lock for imu_queue

	// CSV file handles, ofstream implementation is already buffered.
	// Using pointers because of `container_of`
	ofstream *imu_csv;
	ofstream *left_cam_csv;
	ofstream *right_cam_csv;
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

	create_directories(path + "/mav0/cam0/data");
	er->left_cam_csv = new ofstream{path + "/mav0/cam0/data.csv"};
	*er->left_cam_csv << "#timestamp [ns],filename" CSV_EOL;

	create_directories(path + "/mav0/cam1/data");
	er->right_cam_csv = new ofstream{path + "/mav0/cam1/data.csv"};
	*er->right_cam_csv << "#timestamp [ns],filename" CSV_EOL;
}

static void
euroc_recorder_flush(struct euroc_recorder *er)
{
	vector<xrt_imu_sample> samples;

	{ // Move samples out of imu_queue into vector to minimize mutex contention
		lock_guard lock{er->imu_queue_lock};
		samples.reserve(er->imu_queue.size());
		while (!er->imu_queue.empty()) {
			samples.push_back(er->imu_queue.front());
			er->imu_queue.pop();
		}
	}

	// Write queued IMU samples to csv stream.
	for (xrt_imu_sample &sample : samples) {
		xrt_sink_push_imu(&er->writer_imu_sink, &sample);
	}

	// Flush csv streams. Not necessary, doing it only to increase flush frequency
	er->imu_csv->flush();
	er->left_cam_csv->flush();
	er->right_cam_csv->flush();
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

static void
euroc_recorder_save_frame(euroc_recorder *er, struct xrt_frame *frame, bool is_left)
{
	string cam_name = is_left ? "cam0" : "cam1";
	uint64_t ts = frame->timestamp;

	assert(frame->format == XRT_FORMAT_L8 || frame->format == XRT_FORMAT_R8G8B8); // Only formats supported
	auto img_type = frame->format == XRT_FORMAT_L8 ? CV_8UC1 : CV_8UC3;
	string img_path = er->path + "/mav0/" + cam_name + "/data/" + std::to_string(ts) + ".png";
	cv::Mat img{(int)frame->height, (int)frame->width, img_type, frame->data, frame->stride};
	cv::imwrite(img_path, img);

	ofstream *cam_csv = is_left ? er->left_cam_csv : er->right_cam_csv;
	*cam_csv << ts << "," << ts << ".png" CSV_EOL;
}

extern "C" void
euroc_recorder_save_left(struct xrt_frame_sink *sink, struct xrt_frame *frame)
{
	euroc_recorder *er = container_of(sink, euroc_recorder, writer_left_sink);
	euroc_recorder_flush(er);
	euroc_recorder_save_frame(er, frame, true);
}

extern "C" void
euroc_recorder_save_right(struct xrt_frame_sink *sink, struct xrt_frame *frame)
{
	euroc_recorder *er = container_of(sink, euroc_recorder, writer_right_sink);
	euroc_recorder_save_frame(er, frame, false);
}


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


static void
euroc_recorder_receive_frame(euroc_recorder *er, struct xrt_frame *src_frame, bool is_left)
{
	if (!er->recording) {
		return;
	}

	// Let's clone the frame so that we can release the src_frame quickly
	xrt_frame *copy = nullptr;
	u_frame_clone(src_frame, &copy);

	xrt_sink_push_frame(is_left ? er->writer_queues.left : er->writer_queues.right, copy);

	xrt_frame_reference(&copy, NULL);
}

extern "C" void
euroc_recorder_receive_left(struct xrt_frame_sink *sink, struct xrt_frame *frame)
{
	euroc_recorder *er = container_of(sink, euroc_recorder, cloner_left_sink);
	euroc_recorder_receive_frame(er, frame, true);
}

extern "C" void
euroc_recorder_receive_right(struct xrt_frame_sink *sink, struct xrt_frame *frame)
{
	euroc_recorder *er = container_of(sink, euroc_recorder, cloner_right_sink);
	euroc_recorder_receive_frame(er, frame, false);
}


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
	delete er->left_cam_csv;
	delete er->right_cam_csv;
	delete er;
}


/*
 *
 * Exported functions
 *
 */

extern "C" xrt_slam_sinks *
euroc_recorder_create(struct xrt_frame_context *xfctx, const char *record_path, bool record_from_start)
{
	struct euroc_recorder *er = new euroc_recorder{};

	er->recording = record_from_start;

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
		strftime(datetime, size, "%Y%m%d%H%M%S", localtime(&seconds));
		string default_path = string{"euroc_recording_"} + datetime;
		er->path = default_path;
	}

	if (record_from_start) {
		euroc_recorder_try_mkfiles(er);
	}

	// Setup sink pipeline

	// First, make the public queues that will clone frames in memory so that
	// original frames can be released as soon as possible. Not doing this could
	// result in frame queues from the user being filled up.
	u_sink_queue_create(xfctx, 0, &er->cloner_left_sink, &er->cloner_queues.left);
	u_sink_queue_create(xfctx, 0, &er->cloner_right_sink, &er->cloner_queues.right);
	er->cloner_queues.imu = &er->cloner_imu_sink;

	// Clone samples into heap and release original samples right after
	er->cloner_imu_sink.push_imu = euroc_recorder_receive_imu;
	er->cloner_left_sink.push_frame = euroc_recorder_receive_left;
	er->cloner_right_sink.push_frame = euroc_recorder_receive_right;

	// Then, make a queue to save frame sinks to disk in a separate thread
	u_sink_queue_create(xfctx, 0, &er->writer_left_sink, &er->writer_queues.left);
	u_sink_queue_create(xfctx, 0, &er->writer_right_sink, &er->writer_queues.right);
	er->writer_queues.imu = nullptr;

	// Write cloned samples to disk with these
	er->writer_imu_sink.push_imu = euroc_recorder_save_imu;
	er->writer_left_sink.push_frame = euroc_recorder_save_left;
	er->writer_right_sink.push_frame = euroc_recorder_save_right;

	xrt_slam_sinks *public_sinks = &er->cloner_queues;
	return public_sinks;
}

static void
euroc_recorder_btn_cb(void *ptr)
{
	euroc_recorder *er = (euroc_recorder *)ptr;
	euroc_recorder_try_mkfiles(er);
	er->recording = !er->recording;
	snprintf(er->recording_btn.label, sizeof(er->recording_btn.label),
	         er->recording ? "Stop recording" : "Record EuRoC dataset");
}

extern "C" void
euroc_recorder_add_ui(struct xrt_slam_sinks *public_sinks, void *root)
{
	euroc_recorder *er = container_of(public_sinks, euroc_recorder, cloner_queues);
	er->recording_btn.cb = euroc_recorder_btn_cb;
	er->recording_btn.ptr = er;
	u_var_add_button(root, &er->recording_btn, er->recording ? "Stop recording" : "Record EuRoC dataset");
}
