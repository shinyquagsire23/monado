// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  EuRoC playback functionality
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup drv_euroc
 */

#include "xrt/xrt_frame.h"
#include "xrt/xrt_tracking.h"
#include "xrt/xrt_frameserver.h"
#include "os/os_threading.h"
#include "util/u_debug.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_var.h"
#include "util/u_sink.h"
#include "tracking/t_frame_cv_mat_wrapper.hpp"
#include "math/m_api.h"
#include "math/m_filter_fifo.h"

#include "euroc_driver.h"
#include "euroc_interface.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdint.h>
#include <stdio.h>
#include <fstream>
#include <future>
#include <thread>
#include <inttypes.h>

//! @see euroc_player_playback_config
DEBUG_GET_ONCE_LOG_OPTION(euroc_log, "EUROC_LOG", U_LOGGING_WARN)
DEBUG_GET_ONCE_OPTION(gt_device_name, "EUROC_GT_DEVICE_NAME", nullptr)
DEBUG_GET_ONCE_OPTION(cam_count, "EUROC_CAM_COUNT", nullptr)
DEBUG_GET_ONCE_OPTION(color, "EUROC_COLOR", nullptr)
DEBUG_GET_ONCE_OPTION(gt, "EUROC_GT", nullptr)
DEBUG_GET_ONCE_OPTION(skip_first, "EUROC_SKIP_FIRST", "0%")
DEBUG_GET_ONCE_FLOAT_OPTION(scale, "EUROC_SCALE", 1.0f)
DEBUG_GET_ONCE_BOOL_OPTION(max_speed, "EUROC_MAX_SPEED", false)
DEBUG_GET_ONCE_FLOAT_OPTION(speed, "EUROC_SPEED", 1.0f)
DEBUG_GET_ONCE_BOOL_OPTION(paused, "EUROC_PAUSED", false)
DEBUG_GET_ONCE_BOOL_OPTION(send_all_imus_first, "EUROC_SEND_ALL_IMUS_FIRST", false)
DEBUG_GET_ONCE_BOOL_OPTION(use_source_ts, "EUROC_USE_SOURCE_TS", false)
DEBUG_GET_ONCE_BOOL_OPTION(play_from_start, "EUROC_PLAY_FROM_START", false)
DEBUG_GET_ONCE_BOOL_OPTION(print_progress, "EUROC_PRINT_PROGRESS", false)

#define EUROC_PLAYER_STR "Euroc Player"

//! Match max cameras to slam sinks max camera count
#define EUROC_MAX_CAMS XRT_TRACKING_MAX_SLAM_CAMS

using std::async;
using std::find_if;
using std::ifstream;
using std::is_same_v;
using std::launch;
using std::max_element;
using std::pair;
using std::stof;
using std::string;
using std::to_string;
using std::vector;

using img_sample = pair<timepoint_ns, string>;

using imu_samples = vector<xrt_imu_sample>;
using img_samples = vector<img_sample>;
using gt_trajectory = vector<xrt_pose_sample>;

enum euroc_player_ui_state
{
	UNINITIALIZED = 0,
	NOT_STREAMING,
	STREAM_PLAYING,
	STREAM_PAUSED,
	STREAM_ENDED
};

/*!
 * Euroc player is in charge of the playback of a particular dataset.
 *
 * @implements xrt_fs
 * @implements xrt_frame_node
 */
struct euroc_player
{
	struct xrt_fs base;
	struct xrt_frame_node node;

	// Sinks
	struct xrt_frame_sink cam_sinks[EUROC_MAX_CAMS]; //!< Intermediate sink for each camera frames
	struct xrt_imu_sink imu_sink;                    //!< Intermediate sink for IMU samples
	struct xrt_slam_sinks in_sinks;                  //!< Pointers to intermediate sinks
	struct xrt_slam_sinks out_sinks;                 //!< Pointers to downstream sinks

	enum u_logging_level log_level;               //!< Log messages with this priority and onwards
	struct euroc_player_dataset_info dataset;     //!< Contains information about the source dataset
	struct euroc_player_playback_config playback; //!< Playback information. Prefer to fill it before stream start
	struct xrt_fs_mode mode;                      //!< The only fs mode the euroc dataset provides
	bool is_running;                              //!< Set only at start, stop and end of frameserver stream
	timepoint_ns last_pause_ts;                   //!< Last time the stream was paused
	struct os_thread_helper play_thread;

	//! Next frame number to use, index in `imgs[i]`.
	//! Note that this expects that both cameras provide the same amount of frames.
	//! Furthermore, it is also expected that their timestamps match.
	uint64_t img_seq;          //!< Next frame number to use, index in `imgs[i]`
	uint64_t imu_seq;          //!< Next imu sample number to use, index in `imus`
	imu_samples *imus;         //!< List of all IMU samples read from the dataset
	vector<img_samples> *imgs; //!< List of all image names to read from the dataset per camera
	gt_trajectory *gt;         //!< List of all groundtruth poses read from the dataset

	// Timestamp correction fields (can be disabled through `use_source_ts`)
	timepoint_ns base_ts;   //!< First sample timestamp, stream timestamps are relative to this
	timepoint_ns start_ts;  //!< When did the dataset started to be played
	timepoint_ns offset_ts; //!< Amount of ns to offset start_ns (pauses, skips, etc)

	// UI related fields
	enum euroc_player_ui_state ui_state;
	struct u_var_button start_btn;
	struct u_var_button pause_btn;
	char progress_text[128];
	struct u_sink_debug ui_cam_sinks[EUROC_MAX_CAMS]; //!< Sinks to display cam frames in UI
	struct m_ff_vec3_f32 *gyro_ff;                    //!< Used for displaying IMU data
	struct m_ff_vec3_f32 *accel_ff;                   //!< Same as `gyro_ff`
};

static void
euroc_player_start_btn_cb(void *ptr);
static void
euroc_player_set_ui_state(struct euroc_player *ep, euroc_player_ui_state state);


// Euroc functionality

//! Parse and load all IMU samples into `samples`, assumes data.csv is well formed
//! If `read_n` > 0, read at most that amount of samples
//! Returns whether the appropriate data.csv file could be opened
static bool
euroc_player_preload_imu_data(const string &dataset_path, imu_samples *samples, int64_t read_n = -1)
{
	string csv_filename = dataset_path + "/mav0/imu0/data.csv";
	ifstream fin{csv_filename};
	if (!fin.is_open()) {
		return false;
	}

	constexpr int COLUMN_COUNT = 6; // EuRoC imu columns: ts wx wy wz ax ay az
	string line;
	getline(fin, line); // Skip header line

	while (getline(fin, line) && read_n-- != 0) {
		timepoint_ns timestamp;
		double v[COLUMN_COUNT];
		size_t i = 0;
		size_t j = line.find(',');
		timestamp = stoll(line.substr(i, j));
		for (size_t k = 0; k < COLUMN_COUNT; k++) {
			i = j;
			j = line.find(',', i + 1);
			v[k] = stod(line.substr(i + 1, j));
		}

		xrt_imu_sample sample{timestamp, {v[3], v[4], v[5]}, {v[0], v[1], v[2]}};
		samples->push_back(sample);
	}
	return true;
}

/*!
 * Parse and load ground truth device name and trajectory into `gtdev` and
 * `trajectory` respectively
 *
 * @param[in] dataset_path
 * @param[in, out] gtdev The name of the groundtruth device found in the dataset if any or `nullptr`.
 * Groundtruth data can come from different devices, so we use the first of:
 * 1. The value prespecified in `gtdev`
 * 2. vicon0: found in euroc "vicon room" datasets
 * 3. mocap0: found in TUM-VI datasets with euroc format
 * 4. state_groundtruth_estimate0: found in euroc as a postprocessed ground truth (we only use first 7 columns)
 * 5. leica0: found in euroc "machine hall" datasets, only positional ground truth
 * @param[out] trajectory The read trajectory
 * @param[in] read_n If > 0, read at most that amount of gt poses
 *
 * @returns Whether the appropriate data.csv file could be opened
 */
static bool
euroc_player_preload_gt_data(const string &dataset_path,
                             const char **gtdev,
                             gt_trajectory *trajectory,
                             int64_t read_n = -1)
{
	vector<const char *> gt_devices = {"vicon0", "mocap0", "state_groundtruth_estimate0", "leica0"};
	if (*gtdev != nullptr && !string(*gtdev).empty()) {
		gt_devices.insert(gt_devices.begin(), *gtdev);
	}

	ifstream fin;
	for (const char *device : gt_devices) {
		string csv_filename = dataset_path + "/mav0/" + device + "/data.csv";
		fin = ifstream{csv_filename};
		if (fin.is_open()) {
			*gtdev = device;
			break;
		}
	}

	if (!fin.is_open()) {
		return false;
	}

	constexpr int COLUMN_COUNT = 7; // EuRoC groundtruth columns: ts px py pz qw qx qy qz
	string line;
	getline(fin, line); // Skip header line

	while (getline(fin, line) && read_n-- != 0) {
		timepoint_ns timestamp;
		float v[COLUMN_COUNT] = {0, 0, 0, 1, 0, 0, 0}; // Set identity orientation for leica0
		size_t i = 0;
		size_t j = line.find(',');
		timestamp = stoll(line.substr(i, j));
		for (size_t k = 0; k < COLUMN_COUNT && j != string::npos; k++) {
			i = j;
			j = line.find(',', i + 1);
			v[k] = stof(line.substr(i + 1, j));
		}

		xrt_pose pose = {{v[4], v[5], v[6], v[3]}, {v[0], v[1], v[2]}};
		trajectory->push_back({timestamp, pose});
	}
	return true;
}

//! Parse and load image names and timestamps into `samples`
//! If read_n > 0, read at most that amount of samples
//! Returns whether the appropriate data.csv file could be opened
static bool
euroc_player_preload_img_data(const string &dataset_path, img_samples &samples, size_t cam_id, int64_t read_n = -1)
{
	// Parse image data, assumes data.csv is well formed
	string cam_name = "cam" + to_string(cam_id);
	string imgs_path = dataset_path + "/mav0/" + cam_name + "/data";
	string csv_filename = dataset_path + "/mav0/" + cam_name + "/data.csv";
	ifstream fin{csv_filename};
	if (!fin.is_open()) {
		return false;
	}

	string line;
	getline(fin, line); // Skip header line
	while (getline(fin, line) && read_n-- != 0) {
		size_t i = line.find(',');
		timepoint_ns timestamp = stoll(line.substr(0, i));
		string img_name_tail = line.substr(i + 1);

		// Standard euroc datasets use CRLF line endings, so let's remove the extra '\r'
		if (img_name_tail.back() == '\r') {
			img_name_tail.pop_back();
		}

		string img_name = imgs_path + "/" + img_name_tail;
		img_sample sample{timestamp, img_name};
		samples.push_back(sample);
	}
	return true;
}

//! Trims cameras sequences so that they all start and end at the same sample
//! Note that this function does not guarantee that the dataset is free of framedrops
//! and it assumes it is properly formatted with monotonically increasing timestamps.
static void
euroc_player_match_cams_seqs(struct euroc_player *ep)
{
	// Find newest first timestamp and oldest last timestamp
	timepoint_ns first_ts = INT64_MIN;
	timepoint_ns last_ts = INT64_MAX;
	for (const img_samples &imgs : *ep->imgs) {
		EUROC_ASSERT(!imgs.empty(), "Camera with no samples");

		timepoint_ns cam_first_ts = imgs.front().first;
		if (cam_first_ts > first_ts) {
			first_ts = cam_first_ts;
		}

		timepoint_ns cam_last_ts = imgs.back().first;
		if (cam_last_ts < last_ts) {
			last_ts = cam_last_ts;
		}
	}

	auto is_first = [first_ts](const img_sample &s) { return s.first == first_ts; };
	auto is_last = [last_ts](const img_sample &s) { return s.first == last_ts; };

	for (img_samples &imgs : *ep->imgs) {
		img_samples::iterator new_first = find_if(imgs.begin(), imgs.end(), is_first);
		img_samples::iterator new_last = find_if(imgs.begin(), imgs.end(), is_last);
		EUROC_ASSERT_(new_first != imgs.end() && new_last != imgs.end());
		imgs.assign(new_first, new_last + 1);
	}
}

static void
euroc_player_preload(struct euroc_player *ep)
{
	ep->imus->clear();
	euroc_player_preload_imu_data(ep->dataset.path, ep->imus);

	for (size_t i = 0; i < ep->imgs->size(); i++) {
		ep->imgs->at(i).clear();
		euroc_player_preload_img_data(ep->dataset.path, ep->imgs->at(i), i);
	}

	euroc_player_match_cams_seqs(ep);

	if (ep->dataset.has_gt) {
		ep->gt->clear();
		euroc_player_preload_gt_data(ep->dataset.path, &ep->dataset.gt_device_name, ep->gt);
	}
}

//! Skips the first seconds of the dataset as specified by the user
static void
euroc_player_user_skip(struct euroc_player *ep)
{

	float skip_first_s = 0;
	if (ep->playback.skip_perc) {
		float skip_percentage = ep->playback.skip_first;
		timepoint_ns last_ts = MAX(ep->imgs->at(0).back().first, ep->imus->back().timestamp_ns);
		double dataset_length_s = (last_ts - ep->base_ts) / U_TIME_1S_IN_NS;
		skip_first_s = dataset_length_s * skip_percentage / 100.0f;
	} else {
		skip_first_s = ep->playback.skip_first;
	}

	time_duration_ns skip_first_ns = skip_first_s * U_TIME_1S_IN_NS;
	timepoint_ns skipped_ts = ep->base_ts + skip_first_ns;

	while (ep->imu_seq < ep->imus->size() && ep->imus->at(ep->imu_seq).timestamp_ns < skipped_ts) {
		ep->imu_seq++;
	}

	while (ep->img_seq < ep->imgs->at(0).size() && ep->imgs->at(0).at(ep->img_seq).first < skipped_ts) {
		ep->img_seq++;
	}

	ep->offset_ts -= skip_first_ns / ep->playback.speed;
}

//! Determine and fill attributes of the dataset pointed by `path`
//! Assertion fails if `path` does not point to an euroc dataset
static void
euroc_player_fill_dataset_info(const char *path, euroc_player_dataset_info *dataset)
{
	(void)snprintf(dataset->path, sizeof(dataset->path), "%s", path);
	img_samples samples;
	imu_samples _1;
	gt_trajectory _2;

	size_t i = 0;
	bool has_camera = euroc_player_preload_img_data(dataset->path, samples, i, 1);
	while ((has_camera = euroc_player_preload_img_data(dataset->path, samples, ++i, 0))) {
	}
	size_t cam_count = i;
	EUROC_ASSERT(cam_count <= EUROC_MAX_CAMS, "Increase EUROC_MAX_CAMS (dataset with %zu cams)", cam_count);

	bool has_imu = euroc_player_preload_imu_data(dataset->path, &_1, 0);
	bool has_gt = euroc_player_preload_gt_data(dataset->path, &dataset->gt_device_name, &_2, 0);
	bool is_valid_dataset = cam_count > 0 && has_imu;
	EUROC_ASSERT(is_valid_dataset, "Invalid dataset %s", path);

	cv::Mat first_cam0_img = cv::imread(samples[0].second, cv::IMREAD_ANYCOLOR);
	dataset->cam_count = (int)cam_count;
	dataset->is_colored = first_cam0_img.channels() == 3;
	dataset->has_gt = has_gt;
	dataset->width = first_cam0_img.cols;
	dataset->height = first_cam0_img.rows;
}


// Playback functionality

static struct euroc_player *
euroc_player(struct xrt_fs *xfs)
{
	return (struct euroc_player *)xfs;
}

//! Wrapper around os_monotonic_get_ns to convert to int64_t and check ranges
static timepoint_ns
os_monotonic_get_ts()
{
	uint64_t uts = os_monotonic_get_ns();
	EUROC_ASSERT(uts < INT64_MAX, "Timestamp=%" PRId64 " was greater than INT64_MAX=%ld", uts, INT64_MAX);
	int64_t its = uts;
	return its;
}

//! @returns maps a dataset timestamp to current time
static timepoint_ns
euroc_player_mapped_ts(struct euroc_player *ep, timepoint_ns ts)
{
	timepoint_ns relative_ts = ts - ep->base_ts; // Relative to imu0 first ts
	ep->playback.speed = MAX(ep->playback.speed, 1.0 / 256);
	double speed = ep->playback.speed;
	timepoint_ns mapped_ts = ep->start_ts + ep->offset_ts + relative_ts / speed;
	return mapped_ts;
}

//! Same as @ref euroc_player_mapped_ts but only if playback options allow it.
static timepoint_ns
euroc_player_mapped_playback_ts(struct euroc_player *ep, timepoint_ns ts)
{
	if (ep->playback.use_source_ts) {
		return ts;
	}
	return euroc_player_mapped_ts(ep, ts);
}

static void
euroc_player_load_next_frame(struct euroc_player *ep, int cam_index, struct xrt_frame *&xf)
{
	using xrt::auxiliary::tracking::FrameMat;
	img_sample sample = ep->imgs->at(cam_index).at(ep->img_seq);
	ep->playback.scale = CLAMP(ep->playback.scale, 1.0 / 16, 4);

	// Load will be influenced by these playback options
	bool allow_color = ep->playback.color;
	float scale = ep->playback.scale;

	// Load image from disk
	timepoint_ns timestamp = euroc_player_mapped_playback_ts(ep, sample.first);
	string img_name = sample.second;
	EUROC_TRACE(ep, "cam%d img t = %ld filename = %s", cam_index, timestamp, img_name.c_str());
	cv::ImreadModes read_mode = allow_color ? cv::IMREAD_ANYCOLOR : cv::IMREAD_GRAYSCALE;
	cv::Mat img = cv::imread(img_name, read_mode); // If colored, reads in BGR order

	if (scale != 1.0) {
		cv::Mat tmp;
		cv::resize(img, tmp, cv::Size(), scale, scale);
		img = tmp;
	}

	// Create xrt_frame, it will be freed by FrameMat destructor
	EUROC_ASSERT(xf == NULL || xf->reference.count > 0, "Must be given a valid or NULL frame ptr");
	EUROC_ASSERT(timestamp >= 0, "Unexpected negative timestamp");
	//! @todo Not using xrt_stereo_format because we use two sinks. It would
	//! probably be better to refactor everything to use stereo frames instead.
	FrameMat::Params params{XRT_STEREO_FORMAT_NONE, static_cast<uint64_t>(timestamp)};
	auto wrap = img.channels() == 3 ? FrameMat::wrapR8G8B8 : FrameMat::wrapL8;
	wrap(img, &xf, params);

	// Fields that aren't set by FrameMat
	xf->owner = ep;
	xf->source_timestamp = sample.first;
	xf->source_sequence = ep->img_seq;
	xf->source_id = ep->base.source_id;
}

static void
euroc_player_push_next_frame(struct euroc_player *ep)
{
	int cam_count = ep->playback.cam_count;

	vector<xrt_frame *> xfs(cam_count, nullptr);
	for (int i = 0; i < cam_count; i++) {
		euroc_player_load_next_frame(ep, i, xfs[i]);
	}

	// TODO: Some SLAM systems expect synced frames, but that's not an
	// EuRoC requirement. Adapt to work with unsynced datasets too.
	for (int i = 1; i < cam_count; i++) {
		EUROC_ASSERT(xfs[i - 1]->timestamp == xfs[i]->timestamp, "Unsynced frames");
	}

	ep->img_seq++;

	for (int i = 0; i < cam_count; i++) {
		xrt_sink_push_frame(ep->in_sinks.cams[i], xfs[i]);
	}

	for (int i = 0; i < cam_count; i++) {
		xrt_frame_reference(&xfs[i], NULL);
	}

	size_t fcount = ep->imgs->at(0).size();
	(void)snprintf(ep->progress_text, sizeof(ep->progress_text),
	               "Playback %.2f%% - Frame %" PRId64 "/%" PRId64 " - IMU %" PRId64 "/%" PRId64,
	               float(ep->img_seq) / float(fcount) * 100, ep->img_seq, fcount, ep->imu_seq, ep->imus->size());

	if (ep->playback.print_progress) {
		printf("%s\r", ep->progress_text);
		(void)fflush(stdout);
	}
}

static void
euroc_player_push_next_imu(struct euroc_player *ep)
{
	xrt_imu_sample sample = ep->imus->at(ep->imu_seq++);
	sample.timestamp_ns = euroc_player_mapped_playback_ts(ep, sample.timestamp_ns);
	xrt_sink_push_imu(ep->in_sinks.imu, &sample);
}

static void
euroc_player_push_all_gt(struct euroc_player *ep)
{
	if (!ep->out_sinks.gt) {
		return;
	}

	for (xrt_pose_sample &sample : *ep->gt) {
		sample.timestamp_ns = euroc_player_mapped_playback_ts(ep, sample.timestamp_ns);
		xrt_sink_push_pose(ep->out_sinks.gt, &sample);
	}
}

template <typename SamplesType>
timepoint_ns
euroc_player_get_next_euroc_ts(struct euroc_player *ep)
{
	if constexpr (is_same_v<SamplesType, imu_samples>) {
		return ep->imus->at(ep->imu_seq).timestamp_ns;
	} else {
		return ep->imgs->at(0).at(ep->img_seq).first;
	}
}

template <typename SamplesType>
void
euroc_player_sleep_until_next_sample(struct euroc_player *ep)
{
	using std::chrono::nanoseconds;
	using timepoint_ch = std::chrono::time_point<std::chrono::steady_clock>;
	using std::this_thread::sleep_until;

	timepoint_ns next_sample_euroc_ts = euroc_player_get_next_euroc_ts<SamplesType>(ep);
	timepoint_ns next_sample_mono_ts = euroc_player_mapped_ts(ep, next_sample_euroc_ts);
	timepoint_ch next_sample_chrono_tp{nanoseconds{next_sample_mono_ts}};
	sleep_until(next_sample_chrono_tp);

#ifndef NDEBUG
	// Complain when we are >1ms late. It can happen due to a busy scheduler.
	double oversleep_ms = (os_monotonic_get_ts() - next_sample_mono_ts) / (double)U_TIME_1MS_IN_NS;
	if (abs(oversleep_ms) > 1) {
		string sample_type_name = "frame";
		if constexpr (is_same_v<SamplesType, imu_samples>) {
			sample_type_name = "imu";
		}
		EUROC_DEBUG(ep, "(%s) Woke up %.1fms late", sample_type_name.c_str(), oversleep_ms);
	}
#endif
}

//! Based on the SamplesType to stream, return a set of corresponding entities:
//! the samples vector, sequence number, push and sleep functions.
template <typename SamplesType>
auto
euroc_player_get_stream_set(struct euroc_player *ep)
{
	constexpr bool is_imu = is_same_v<SamplesType, imu_samples>;
	const SamplesType *samples;
	if constexpr (is_imu) {
		samples = ep->imus;
	} else {
		samples = &ep->imgs->at(0);
	}
	uint64_t *sample_seq = is_imu ? &ep->imu_seq : &ep->img_seq;
	auto push_next_sample = is_imu ? euroc_player_push_next_imu : euroc_player_push_next_frame;
	auto sleep_until_next_sample = euroc_player_sleep_until_next_sample<SamplesType>;
	return make_tuple(samples, sample_seq, push_next_sample, sleep_until_next_sample);
}

template <typename SamplesType>
static void
euroc_player_stream_samples(struct euroc_player *ep)
{
	// These fields correspond to IMU or frame streams depending on SamplesType
	const auto [samples, sample_seq, push_next_sample, sleep_until_next_sample] =
	    euroc_player_get_stream_set<SamplesType>(ep);

	while (*sample_seq < samples->size() && ep->is_running) {
		while (ep->playback.paused) {
			constexpr int64_t PAUSE_POLL_INTERVAL_NS = 15L * U_TIME_1MS_IN_NS;
			os_nanosleep(PAUSE_POLL_INTERVAL_NS);
		}

		if (!ep->playback.max_speed) {
			sleep_until_next_sample(ep);
		}

		push_next_sample(ep);
	}
}

static void *
euroc_player_stream(void *ptr)
{
	struct xrt_fs *xfs = (struct xrt_fs *)ptr;
	struct euroc_player *ep = euroc_player(xfs);
	EUROC_INFO(ep, "Starting euroc playback");

	euroc_player_preload(ep);
	ep->base_ts = MIN(ep->imgs->at(0).at(0).first, ep->imus->at(0).timestamp_ns);
	ep->start_ts = os_monotonic_get_ts();
	euroc_player_user_skip(ep);

	// Push all IMU samples now if requested
	if (ep->playback.send_all_imus_first) {
		while (ep->imu_seq < ep->imus->size()) {
			euroc_player_push_next_imu(ep);
		}
	}

	// Push ground truth trajectory now if available (and not disabled)
	if (ep->playback.gt) {
		euroc_player_push_all_gt(ep);
	}

	// Launch image and IMU producers
	auto serve_imus = async(launch::async, [ep] { euroc_player_stream_samples<imu_samples>(ep); });
	auto serve_imgs = async(launch::async, [ep] { euroc_player_stream_samples<img_samples>(ep); });
	// Note that the only fields of `ep` being modified in the threads are: img_seq, imu_seq and
	// progress_text in single locations, thus no race conditions should occur.

	// Wait for the end of both streams
	serve_imgs.get();
	serve_imus.get();

	ep->is_running = false;

	EUROC_INFO(ep, "Euroc dataset playback finished");
	euroc_player_set_ui_state(ep, STREAM_ENDED);

	return NULL;
}


// Frame server functionality

static bool
euroc_player_enumerate_modes(struct xrt_fs *xfs, struct xrt_fs_mode **out_modes, uint32_t *out_count)
{
	struct euroc_player *ep = euroc_player(xfs);

	// Should be freed by caller
	struct xrt_fs_mode *modes = U_TYPED_ARRAY_CALLOC(struct xrt_fs_mode, 1);
	EUROC_ASSERT(modes != NULL, "Unable to calloc euroc playback modes");

	// At first, it would sound like a good idea to list all possible playback
	// modes here, however it gets more troublesome than it is worth, and there
	// doesn't seem to be a good reason to use this feature here. Having said
	// that, a basic fs mode will be provided, which consists of only the original
	// properties of the dataset, and ignores the other playback options that can
	// be tweaked in the UI.
	modes[0] = ep->mode;

	*out_modes = modes;
	*out_count = 1;

	return true;
}

static bool
euroc_player_configure_capture(struct xrt_fs *xfs, struct xrt_fs_capture_parameters *cp)
{
	// struct euroc_player *ep = euroc_player(xfs);
	EUROC_ASSERT(false, "Not implemented");
	return false;
}

#define DEFINE_RECEIVE_CAM(cam_id)                                                                                     \
	static void receive_cam##cam_id(struct xrt_frame_sink *sink, struct xrt_frame *xf)                             \
	{                                                                                                              \
		struct euroc_player *ep = container_of(sink, struct euroc_player, cam_sinks[cam_id]);                  \
		EUROC_TRACE(ep, "cam%d img t=%ld source_t=%ld", cam_id, xf->timestamp, xf->source_timestamp);          \
		u_sink_debug_push_frame(&ep->ui_cam_sinks[cam_id], xf);                                                \
		if (ep->out_sinks.cams[cam_id]) {                                                                      \
			xrt_sink_push_frame(ep->out_sinks.cams[cam_id], xf);                                           \
		}                                                                                                      \
	}


DEFINE_RECEIVE_CAM(0)
DEFINE_RECEIVE_CAM(1)
DEFINE_RECEIVE_CAM(2)
DEFINE_RECEIVE_CAM(3)
DEFINE_RECEIVE_CAM(4)

//! Be sure to define the same number of definition as EUROC_MAX_CAMS and to add them to `receive_cam`.
static void (*receive_cam[EUROC_MAX_CAMS])(struct xrt_frame_sink *, struct xrt_frame *) = {
    receive_cam0, //
    receive_cam1, //
    receive_cam2, //
    receive_cam3, //
    receive_cam4, //
};

static void
receive_imu_sample(struct xrt_imu_sink *sink, struct xrt_imu_sample *s)
{
	struct euroc_player *ep = container_of(sink, struct euroc_player, imu_sink);

	timepoint_ns ts = s->timestamp_ns;
	xrt_vec3_f64 a = s->accel_m_s2;
	xrt_vec3_f64 w = s->gyro_rad_secs;

	// UI log
	const xrt_vec3 gyro{(float)w.x, (float)w.y, (float)w.z};
	const xrt_vec3 accel{(float)a.x, (float)a.y, (float)a.z};
	m_ff_vec3_f32_push(ep->gyro_ff, &gyro, ts);
	m_ff_vec3_f32_push(ep->accel_ff, &accel, ts);

	// Trace log
	EUROC_TRACE(ep, "imu t=%ld ax=%f ay=%f az=%f wx=%f wy=%f wz=%f", ts, a.x, a.y, a.z, w.x, w.y, w.z);
	if (ep->out_sinks.imu) {
		xrt_sink_push_imu(ep->out_sinks.imu, s);
	}
}

//! This is the @ref xrt_fs stream start method, however as the euroc playback
//! is heavily customizable, it will be managed through the UI. So, unless
//! EUROC_PLAY_FROM_START is set, this will not start outputting frames until
//! the user clicks the start button.
static bool
euroc_player_stream_start(struct xrt_fs *xfs,
                          struct xrt_frame_sink *xs,
                          enum xrt_fs_capture_type capture_type,
                          uint32_t descriptor_index)
{
	struct euroc_player *ep = euroc_player(xfs);

	if (xs == NULL && capture_type == XRT_FS_CAPTURE_TYPE_TRACKING) {
		EUROC_INFO(ep, "Starting Euroc Player in tracking mode");
		if (ep->out_sinks.cams[0] == NULL) {
			EUROC_WARN(ep, "No cam0 sink provided, will keep running but tracking is unlikely to work");
		}
		if (ep->playback.play_from_start) {
			euroc_player_start_btn_cb(ep);
		}
	} else if (xs != NULL && capture_type == XRT_FS_CAPTURE_TYPE_CALIBRATION) {
		EUROC_INFO(ep, "Starting Euroc Player in calibration mode, will stream only cam0 frames right away");
		ep->out_sinks.cams[0] = xs;
		euroc_player_start_btn_cb(ep);
	} else {
		EUROC_ASSERT(false, "Unsupported stream configuration xs=%p capture_type=%d", (void *)xs, capture_type);
		return false;
	}

	ep->is_running = true;
	return ep->is_running;
}

static bool
euroc_player_slam_stream_start(struct xrt_fs *xfs, struct xrt_slam_sinks *sinks)
{
	struct euroc_player *ep = euroc_player(xfs);
	ep->out_sinks = *sinks;
	return euroc_player_stream_start(xfs, NULL, XRT_FS_CAPTURE_TYPE_TRACKING, 0);
}

static bool
euroc_player_stream_stop(struct xrt_fs *xfs)
{
	struct euroc_player *ep = euroc_player(xfs);
	ep->is_running = false;

	// Destroy also stops the thread.
	os_thread_helper_destroy(&ep->play_thread);

	return true;
}

static bool
euroc_player_is_running(struct xrt_fs *xfs)
{
	struct euroc_player *ep = euroc_player(xfs);
	return ep->is_running;
}


// Frame node functionality

static void
euroc_player_break_apart(struct xrt_frame_node *node)
{
	struct euroc_player *ep = container_of(node, struct euroc_player, node);
	euroc_player_stream_stop(&ep->base);
}

static void
euroc_player_destroy(struct xrt_frame_node *node)
{
	struct euroc_player *ep = container_of(node, struct euroc_player, node);

	delete ep->gt;
	delete ep->imus;
	delete ep->imgs;

	u_var_remove_root(ep);
	for (int i = 0; i < ep->dataset.cam_count; i++) {
		u_sink_debug_destroy(&ep->ui_cam_sinks[i]);
	}
	m_ff_vec3_f32_free(&ep->gyro_ff);
	m_ff_vec3_f32_free(&ep->accel_ff);

	free(ep);
}


// UI functionality

static void
euroc_player_set_ui_state(struct euroc_player *ep, euroc_player_ui_state state)
{
	// -> UNINITIALIZED -> NOT_STREAMING -> STREAM_PLAYING <-> STREAM_PAUSED
	//                                              └> STREAM_ENDED <┘
	euroc_player_ui_state prev_state = ep->ui_state;
	if (state == NOT_STREAMING) {
		EUROC_ASSERT_(prev_state == UNINITIALIZED);
		ep->pause_btn.disabled = true;
		snprintf(ep->progress_text, sizeof(ep->progress_text), "Stream has not started");
	} else if (state == STREAM_PLAYING) {
		EUROC_ASSERT_(prev_state == NOT_STREAMING || prev_state == STREAM_PAUSED);
		ep->start_btn.disabled = true;
		ep->pause_btn.disabled = false;
		snprintf(ep->pause_btn.label, sizeof(ep->pause_btn.label), "Pause");
	} else if (state == STREAM_PAUSED) {
		EUROC_ASSERT_(prev_state == STREAM_PLAYING);
		snprintf(ep->pause_btn.label, sizeof(ep->pause_btn.label), "Resume");
	} else if (state == STREAM_ENDED) {
		EUROC_ASSERT_(prev_state == STREAM_PLAYING || prev_state == STREAM_PAUSED);
		ep->pause_btn.disabled = true;
	} else {
		EUROC_ASSERT(false, "Unexpected UI state transition from %d to %d", prev_state, state);
	}
	ep->ui_state = state;
}

static void
euroc_player_start_btn_cb(void *ptr)
{
	struct euroc_player *ep = (struct euroc_player *)ptr;

	int ret = 0;
	ret |= os_thread_helper_init(&ep->play_thread);
	ret |= os_thread_helper_start(&ep->play_thread, euroc_player_stream, ep);
	EUROC_ASSERT(ret == 0, "Thread launch failure");

	euroc_player_set_ui_state(ep, STREAM_PLAYING);
}

static void
euroc_player_pause_btn_cb(void *ptr)
{
	//! @note: if you have groundtruth, pausing will unsync it from the tracker.

	struct euroc_player *ep = (struct euroc_player *)ptr;
	ep->playback.paused = !ep->playback.paused;

	if (ep->playback.paused) {
		ep->last_pause_ts = os_monotonic_get_ts();
	} else {
		time_duration_ns pause_length = os_monotonic_get_ts() - ep->last_pause_ts;
		ep->offset_ts += pause_length;
	}

	euroc_player_set_ui_state(ep, ep->playback.paused ? STREAM_PAUSED : STREAM_PLAYING);
}

static void
euroc_player_setup_gui(struct euroc_player *ep)
{
	// Set sinks to display in UI
	for (int i = 0; i < ep->dataset.cam_count; i++) {
		u_sink_debug_init(&ep->ui_cam_sinks[i]);
	}
	m_ff_vec3_f32_alloc(&ep->gyro_ff, 1000);
	m_ff_vec3_f32_alloc(&ep->accel_ff, 1000);

	// Set button callbacks
	ep->start_btn.cb = euroc_player_start_btn_cb;
	ep->start_btn.ptr = ep;
	ep->pause_btn.cb = euroc_player_pause_btn_cb;
	ep->pause_btn.ptr = ep;
	euroc_player_set_ui_state(ep, NOT_STREAMING);

	// Add UI wigets
	u_var_add_root(ep, "Euroc Player", false);
	u_var_add_ro_text(ep, ep->dataset.path, "Dataset");
	u_var_add_ro_text(ep, ep->progress_text, "Progress");
	u_var_add_button(ep, &ep->start_btn, "Start");
	u_var_add_button(ep, &ep->pause_btn, "Pause");
	u_var_add_log_level(ep, &ep->log_level, "Log level");

	u_var_add_gui_header(ep, NULL, "Playback Options");
	u_var_add_ro_text(ep, "Set these before starting the stream", "Note");
	u_var_add_i32(ep, &ep->playback.cam_count, "Use N cams (if available)");
	u_var_add_bool(ep, &ep->playback.color, "Color (if available)");
	u_var_add_bool(ep, &ep->playback.gt, "Groundtruth (if available)");
	u_var_add_bool(ep, &ep->playback.skip_perc, "Skip percentage, otherwise skips seconds");
	u_var_add_f32(ep, &ep->playback.skip_first, "How much to skip");
	u_var_add_f32(ep, &ep->playback.scale, "Scale");
	u_var_add_bool(ep, &ep->playback.max_speed, "Max speed");
	u_var_add_f64(ep, &ep->playback.speed, "Speed");
	u_var_add_bool(ep, &ep->playback.send_all_imus_first, "Send all IMU samples first");
	u_var_add_bool(ep, &ep->playback.use_source_ts, "Use original timestamps");

	u_var_add_gui_header(ep, NULL, "Streams");
	u_var_add_ro_ff_vec3_f32(ep, ep->gyro_ff, "Gyroscope");
	u_var_add_ro_ff_vec3_f32(ep, ep->accel_ff, "Accelerometer");
	for (int i = 0; i < ep->dataset.cam_count; i++) {
		char label[] = "Camera NNNNNNNNNN";
		(void)snprintf(label, sizeof(label), "Camera %d", i);
		u_var_add_sink_debug(ep, &ep->ui_cam_sinks[i], label);
	}
}

extern "C" void
euroc_player_fill_default_config_for(struct euroc_player_config *config, const char *dataset_path)
{
	struct euroc_player_dataset_info dataset = {};
	dataset.gt_device_name = debug_get_option_gt_device_name();
	euroc_player_fill_dataset_info(dataset_path, &dataset);

	struct euroc_player_playback_config playback = {};
	const char *cam_count = debug_get_option_cam_count();
	const char *color = debug_get_option_color();
	const char *gt = debug_get_option_gt();
	const char *skip_option = debug_get_option_skip_first();
	playback.cam_count = (int)debug_string_to_num(cam_count, dataset.cam_count);
	playback.color = color == nullptr ? dataset.is_colored : debug_string_to_bool(color);
	playback.gt = gt == nullptr ? dataset.has_gt : debug_string_to_bool(gt);
	playback.skip_perc = string(skip_option).back() == '%';
	playback.skip_first = stof(skip_option);
	playback.scale = debug_get_float_option_scale();
	playback.max_speed = debug_get_bool_option_max_speed();
	playback.speed = debug_get_float_option_speed();
	playback.paused = debug_get_bool_option_paused();
	playback.send_all_imus_first = debug_get_bool_option_send_all_imus_first();
	playback.use_source_ts = debug_get_bool_option_use_source_ts();
	playback.play_from_start = debug_get_bool_option_play_from_start();
	playback.print_progress = debug_get_bool_option_print_progress();

	config->log_level = debug_get_log_option_euroc_log();
	config->dataset = dataset;
	config->playback = playback;
}

// Euroc driver creation

extern "C" struct xrt_fs *
euroc_player_create(struct xrt_frame_context *xfctx, const char *path, struct euroc_player_config *config)
{
	struct euroc_player *ep = U_TYPED_CALLOC(struct euroc_player);

	struct euroc_player_config *default_config = nullptr;
	if (config == nullptr) {
		default_config = U_TYPED_CALLOC(struct euroc_player_config);
		euroc_player_fill_default_config_for(default_config, path);
		config = default_config;
	}

	ep->log_level = config->log_level;
	ep->dataset = config->dataset;
	ep->playback = config->playback;

	if (default_config != nullptr) {
		free(default_config);
	}

	ep->mode = xrt_fs_mode{
	    ep->dataset.width,
	    ep->dataset.height,
	    ep->dataset.is_colored ? XRT_FORMAT_R8G8B8 : XRT_FORMAT_R8,
	    // Stereo euroc *is* supported, but we don't expose that through the
	    // xrt_fs interface as it will be managed through two different sinks.
	    XRT_STEREO_FORMAT_NONE,
	};
	EUROC_INFO(ep, "dataset information\n\tpath: %s\n\tcam_count: %d, is_colored: %d, width: %d, height: %d",
	           ep->dataset.path, ep->dataset.cam_count, ep->dataset.is_colored, ep->dataset.width,
	           ep->dataset.height);

	// Using pointers to not mix vector with a C-compatible struct
	ep->gt = new gt_trajectory{};
	ep->imus = new imu_samples{};
	ep->imgs = new vector<img_samples>(ep->dataset.cam_count);

	euroc_player_setup_gui(ep);

	EUROC_ASSERT(receive_cam[ARRAY_SIZE(receive_cam) - 1] != nullptr, "See `receive_cam` docs");
	ep->in_sinks.cam_count = ep->dataset.cam_count;
	for (int i = 0; i < ep->dataset.cam_count; i++) {
		ep->cam_sinks[i].push_frame = receive_cam[i];
		ep->in_sinks.cams[i] = &ep->cam_sinks[i];
	}
	ep->imu_sink.push_imu = receive_imu_sample;
	ep->in_sinks.imu = &ep->imu_sink;

	struct xrt_fs *xfs = &ep->base;
	xfs->enumerate_modes = euroc_player_enumerate_modes;
	xfs->configure_capture = euroc_player_configure_capture;
	xfs->stream_start = euroc_player_stream_start;
	xfs->slam_stream_start = euroc_player_slam_stream_start;
	xfs->stream_stop = euroc_player_stream_stop;
	xfs->is_running = euroc_player_is_running;

	(void)snprintf(xfs->name, sizeof(xfs->name), EUROC_PLAYER_STR);
	(void)snprintf(xfs->product, sizeof(xfs->product), EUROC_PLAYER_STR " Product");
	(void)snprintf(xfs->manufacturer, sizeof(xfs->manufacturer), EUROC_PLAYER_STR " Manufacturer");
	(void)snprintf(xfs->serial, sizeof(xfs->serial), EUROC_PLAYER_STR " Serial");
	xfs->source_id = 0xECD0FEED;

	struct xrt_frame_node *xfn = &ep->node;
	xfn->break_apart = euroc_player_break_apart;
	xfn->destroy = euroc_player_destroy;

	xrt_frame_context_add(xfctx, &ep->node);

	EUROC_DEBUG(ep, "Euroc player created");

	return xfs;
}
