// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  EuRoC playback functionality
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup drv_euroc
 */

#include "xrt/xrt_tracking.h"
#include "xrt/xrt_frameserver.h"
#include "os/os_threading.h"
#include "util/u_debug.h"
#include "util/u_misc.h"
#include "util/u_var.h"
#include "util/u_sink.h"
#include "tracking/t_frame_cv_mat_wrapper.hpp"
#include "math/m_filter_fifo.h"

#include "euroc_driver.h"

#include <stdio.h>
#include <fstream>

#define EUROC_PLAYER_STR "Euroc Player"
#define CLAMP(X, A, B) (MIN(MAX((X), (A)), (B)))


typedef std::pair<timepoint_ns, std::string> img_sample;
typedef std::vector<xrt_imu_sample> imu_samples;
typedef std::vector<img_sample> img_samples;

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
	struct xrt_frame_sink left_sink;  //!< Intermediate sink for left camera frames
	struct xrt_frame_sink right_sink; //!< Intermediate sink for right camera frames
	struct xrt_imu_sink imu_sink;     //!< Intermediate sink for IMU samples
	struct xrt_slam_sinks in_sinks;   //!< Pointers to intermediate sinks
	struct xrt_slam_sinks out_sinks;  //!< Pointers to downstream sinks

	struct os_thread_helper play_thread;
	enum u_logging_level log_level;
	struct xrt_fs_mode mode; //!< The only fs mode the euroc dataset provides
	bool is_running;         //!< Set only at start and stop of frameserver stream

	//! Contains information about the source dataset; set only at start
	struct
	{
		char path[256];
		bool is_stereo;
		bool is_colored;
		uint32_t width;
		uint32_t height;
	} dataset;

	//! Next frame number to use, index in `left_imgs` and `right_imgs`.
	//! Note that this expects that both cameras provide the same amount of frames.
	//! Furthermore, it is also expected that their timestamps match.
	uint64_t img_seq;
	uint64_t imu_seq;        //!< Next imu sample number to use, index in `imus`
	imu_samples *imus;       //!< List of all IMU samples read from the dataset
	img_samples *left_imgs;  //!< List of all image names to read from the dataset
	img_samples *right_imgs; //!< List of all image names to read from the dataset

	// Timestamp correction fields (can be disabled through `use_source_ts`)
	timepoint_ns base_ts;   //!< First imu0 timestamp, samples timestamps are relative to this
	timepoint_ns start_ts;  //!< When did the dataset started to be played
	timepoint_ns offset_ts; //!< Amount of ns to offset start_ns (pauses, skips, etc)

	//! Playback information.
	//! Prefer to fill it before starting to push frames. Modifying them on
	//! runtime will work with the debug sinks but probably not elsewhere
	struct
	{
		bool stereo;              //!< Whether to stream both left and right sinks or only left
		bool color;               //!< If RGB available but this is false, images will be loaded in grayscale
		float skip_first_s;       //!< Amount of initial seconds of the dataset to skip
		float scale;              //!< Scale of each frame; e.g., 0.5 (half), 1.0 (avoids resize)
		double speed;             //!< Intended reproduction speed, could be slower due to read times
		bool send_all_imus_first; //!< If enabled all imu samples will be sent before img samples
		bool paused;              //!< Whether to pause the playback
		bool use_source_ts;       //!< If true, use the original timestamps from the dataset
	} playback;

	// UI related fields
	enum euroc_player_ui_state ui_state;
	struct u_var_button start_btn;
	struct u_var_button pause_btn;
	char progress_text[128];
	struct u_sink_debug ui_left_sink;  //!< Sink to display left frames in UI
	struct u_sink_debug ui_right_sink; //!< Sink to display right frames in UI
	struct m_ff_vec3_f32 *gyro_ff;     //!< Used for displaying IMU data
	struct m_ff_vec3_f32 *accel_ff;    //!< Same as `gyro_ff`
};

static void
euroc_player_start_btn_cb(void *ptr);
static void
euroc_player_set_ui_state(struct euroc_player *ep, euroc_player_ui_state state);


// Euroc functionality

//! Parse and load all IMU samples into `samples`, assumes data.csv is well formed
//! If `ep` is not null, will set `ep->base_ts` with the first timestamp read
//! If `read_n` > 0, read at most that amount of samples
//! Returns whether the appropriate data.csv file could be opened
static bool
euroc_player_preload_imu_data(struct euroc_player *ep,
                              std::string dataset_path,
                              imu_samples *samples,
                              int64_t read_n = -1)
{
	std::string csv_filename = dataset_path + "/mav0/imu0/data.csv";
	std::ifstream fin{csv_filename};
	if (!fin.is_open()) {
		return false;
	}

	std::string line;
	std::getline(fin, line); // Skip header line
	bool set_base_ts = ep != NULL;

	while (std::getline(fin, line) && read_n-- != 0) {
		timepoint_ns timestamp;
		double v[6];
		size_t i = 0;
		size_t j = line.find(',');
		timestamp = std::stoll(line.substr(i, j));
		for (size_t k = 0; k < 6; k++) {
			i = j;
			j = line.find(',', i + 1);
			v[k] = std::stod(line.substr(i + 1, j));
		}

		// Save first IMU sample timestamp
		if (set_base_ts) {
			ep->base_ts = timestamp;
			set_base_ts = false;
		}

		xrt_imu_sample sample{timestamp, {v[3], v[4], v[5]}, {v[0], v[1], v[2]}};
		samples->push_back(sample);
	}
	return true;
}

//! Parse and load image names and timestamps into `samples`
//! If read_n > 0, read at most that amount of samples
//! Returns whether the appropriate data.csv file could be opened
static bool
euroc_player_preload_img_data(std::string dataset_path, img_samples *samples, bool is_left, int64_t read_n = -1)
{
	// Parse image data, assumes data.csv is well formed
	std::string cam_name = is_left ? "cam0" : "cam1";
	std::string imgs_path = dataset_path + "/mav0/" + cam_name + "/data";
	std::string csv_filename = dataset_path + "/mav0/" + cam_name + "/data.csv";
	std::ifstream fin{csv_filename};
	if (!fin.is_open()) {
		return false;
	}

	std::string line;
	std::getline(fin, line); // Skip header line
	while (std::getline(fin, line) && read_n-- != 0) {
		size_t i = line.find(',');
		timepoint_ns timestamp = std::stoll(line.substr(0, i));
		std::string img_name_tail = line.substr(i + 1);

		// Standard euroc datasets use CRLF line endings, so let's remove the extra '\r'
		if (img_name_tail.back() == '\r') {
			img_name_tail.pop_back();
		}

		std::string img_name = imgs_path + "/" + img_name_tail;
		img_sample sample{timestamp, img_name};
		samples->push_back(sample);
	}
	return true;
}

static void
euroc_player_preload(struct euroc_player *ep)
{
	ep->imus->clear();
	euroc_player_preload_imu_data(ep, ep->dataset.path, ep->imus);

	ep->left_imgs->clear();
	euroc_player_preload_img_data(ep->dataset.path, ep->left_imgs, true);

	if (ep->dataset.is_stereo) {
		ep->right_imgs->clear();
		euroc_player_preload_img_data(ep->dataset.path, ep->right_imgs, false);
	}
}

//! Skips the first seconds of the dataset as specified by the user
static void
euroc_player_user_skip(struct euroc_player *ep)
{
	timepoint_ns skip_first_ns = ep->playback.skip_first_s * 1000 * 1000 * 1000;
	timepoint_ns skipped_ts = ep->base_ts + skip_first_ns;

	while (ep->imus->at(ep->imu_seq).timestamp_ns < skipped_ts) {
		ep->imu_seq++;
	}

	while (ep->left_imgs->at(ep->img_seq).first < skipped_ts) {
		ep->img_seq++;
	}

	ep->offset_ts -= skip_first_ns / ep->playback.speed;
}

//! Determine and fill attributes of the dataset pointed by `path`
//! Assertion fails if `path` does not point to an euroc dataset
static void
euroc_player_fill_dataset_info(struct euroc_player *ep, const char *path)
{
	const char *euroc_path = debug_get_option_euroc_path();
	EUROC_ASSERT(strcmp(euroc_path, path) == 0, "Unexpected path=%s differs from EUROC_PATH=%s", path, euroc_path);

	snprintf(ep->dataset.path, sizeof(ep->dataset.path), "%s", path);
	img_samples samples;
	imu_samples _;
	bool has_right_camera = euroc_player_preload_img_data(ep->dataset.path, &samples, false, 0);
	bool has_left_camera = euroc_player_preload_img_data(ep->dataset.path, &samples, true, 1);
	bool has_imu = euroc_player_preload_imu_data(NULL, ep->dataset.path, &_, 0);
	bool is_valid_dataset = has_left_camera && has_imu;
	EUROC_ASSERT(is_valid_dataset, "Invalid dataset %s", path);

	cv::Mat first_left_img = cv::imread(samples[0].second, cv::IMREAD_ANYCOLOR);
	ep->dataset.is_stereo = has_right_camera;
	ep->dataset.is_colored = first_left_img.channels() == 3;
	ep->dataset.width = first_left_img.cols;
	ep->dataset.height = first_left_img.rows;
	EUROC_INFO(ep, "dataset information\n\tpath: %s\n\tis_stereo: %d, is_colored: %d, width: %d, height: %d",
	           ep->dataset.path, ep->dataset.is_stereo, ep->dataset.is_colored, ep->dataset.width,
	           ep->dataset.height);
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
	EUROC_ASSERT(uts < INT64_MAX, "Timestamp=%lu was greater than INT64_MAX=%ld", uts, INT64_MAX);
	int64_t its = uts;
	return its;
}

//! @returns maps a timestamp to current time (wrt. ep->start_ts)
//! from the original euroc timestamp (uses first imu0 timestamp as base time)
static timepoint_ns
euroc_player_mapped_ts(struct euroc_player *ep, timepoint_ns ts)
{
	if (ep->playback.use_source_ts) {
		return ts;
	}

	timepoint_ns relative_ts = ts - ep->base_ts; // Relative to imu0 first ts
	ep->playback.speed = MAX(ep->playback.speed, 1.0 / 256);
	double speed = ep->playback.speed;
	timepoint_ns mapped_ts = ep->start_ts + ep->offset_ts + relative_ts / speed;
	return mapped_ts;
}

static void
euroc_player_load_next_frame(struct euroc_player *ep, bool is_left, struct xrt_frame *&xf)
{
	using xrt::auxiliary::tracking::FrameMat;
	img_sample sample = is_left ? ep->left_imgs->at(ep->img_seq) : ep->right_imgs->at(ep->img_seq);
	ep->playback.scale = CLAMP(ep->playback.scale, 1.0 / 16, 4);

	// Load will be influenced by these playback options
	bool allow_color = ep->playback.color;
	float scale = ep->playback.scale;

	// Load image from disk
	timepoint_ns timestamp = euroc_player_mapped_ts(ep, sample.first);
	std::string img_name = sample.second;
	EUROC_TRACE(ep, "%s img t = %ld filename = %s", is_left ? "left" : "right", timestamp, img_name.c_str());
	cv::ImreadModes read_mode = allow_color ? cv::IMREAD_ANYCOLOR : cv::IMREAD_GRAYSCALE;
	cv::Mat img = cv::imread(img_name, read_mode); // If colored, reads in BGR order

	if (scale != 1.0) {
		cv::Mat tmp;
		cv::resize(img, tmp, cv::Size(), scale, scale);
		img = tmp;
	}

	// Create xrt_frame, it will be freed by FrameMat destructor
	EUROC_ASSERT(xf == NULL || xf->reference.count > 0, "Must be given a valid or NULL frame ptr");
	EUROC_ASSERT(timestamp > 0, "Unexpected negative timestamp");
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

static bool
euroc_player_is_imu_next(struct euroc_player *ep)
{
	bool prioritize_imus = ep->playback.send_all_imus_first;
	bool more_imus = ep->imu_seq < ep->imus->size();
	if (more_imus && prioritize_imus) {
		return true;
	}

	bool more_imgs = ep->img_seq < ep->left_imgs->size();
	timepoint_ns imu_ts = more_imus ? ep->imus->at(ep->imu_seq).timestamp_ns : INT64_MAX;
	timepoint_ns img_ts = more_imgs ? ep->left_imgs->at(ep->img_seq).first : INT64_MAX;
	return imu_ts < img_ts;
}

static void
euroc_player_push_next_sample(struct euroc_player *ep)
{
	bool stereo = ep->playback.stereo;

	// Push next IMU sample
	if (euroc_player_is_imu_next(ep)) {
		xrt_imu_sample sample = ep->imus->at(ep->imu_seq++);
		sample.timestamp_ns = euroc_player_mapped_ts(ep, sample.timestamp_ns);
		xrt_sink_push_imu(ep->in_sinks.imu, &sample);
		return;
	}

	// Push next frame(s)
	struct xrt_frame *left_xf = NULL, *right_xf = NULL;
	euroc_player_load_next_frame(ep, true, left_xf);
	if (stereo) {
		// TODO: Some SLAM systems expect synced frames, but that's not an
		// EuRoC requirement. Adapt to work with unsynced datasets too.
		euroc_player_load_next_frame(ep, false, right_xf);
		EUROC_ASSERT(left_xf->timestamp == right_xf->timestamp, "Unsynced stereo frames");
	}
	ep->img_seq++;

	xrt_sink_push_frame(ep->in_sinks.left, left_xf);
	if (stereo) {
		xrt_sink_push_frame(ep->in_sinks.right, right_xf);
	}

	// We are now done with the frames, unreference them so
	// they can be freed if all consumers are done with them.
	xrt_frame_reference(&left_xf, NULL);
	xrt_frame_reference(&right_xf, NULL);

	snprintf(ep->progress_text, sizeof(ep->progress_text), "Frames %lu/%lu - IMUs %lu/%lu", ep->img_seq,
	         ep->left_imgs->size(), ep->imu_seq, ep->imus->size());

	// Determine how much to sleep until next frame
	if (ep->img_seq >= ep->left_imgs->size()) {
		return;
	}
	timepoint_ns next_frame_ts = euroc_player_mapped_ts(ep, ep->left_imgs->at(ep->img_seq).first);
	timepoint_ns now = os_monotonic_get_ts();
	int32_t frame_delay_ns = MAX(next_frame_ts - now, 0);
	os_nanosleep(frame_delay_ns);
}

static bool
euroc_player_is_paused(struct euroc_player *ep)
{
	if (!ep->playback.paused) {
		return false;
	}

	timepoint_ns pre_pause = os_monotonic_get_ts();
	os_nanosleep(200 * 1000 * 1000);
	timepoint_ns pos_pause = os_monotonic_get_ts();
	timepoint_ns pause_length = pos_pause - pre_pause;
	ep->offset_ts += pause_length;
	return true;
}

static void *
euroc_player_mainloop(void *ptr)
{
	struct xrt_fs *xfs = (struct xrt_fs *)ptr;
	struct euroc_player *ep = euroc_player(xfs);
	EUROC_INFO(ep, "Starting euroc playback");

	euroc_player_preload(ep);
	euroc_player_user_skip(ep);

	ep->start_ts = os_monotonic_get_ts();

	bool more_imus = ep->imu_seq < ep->imus->size();
	bool more_imgs = ep->img_seq < ep->left_imgs->size();

	while (ep->is_running && (more_imus || more_imgs)) {
		if (euroc_player_is_paused(ep)) {
			continue;
		}

		euroc_player_push_next_sample(ep);

		more_imus = ep->imu_seq < ep->imus->size();
		more_imgs = ep->img_seq < ep->left_imgs->size();
	}

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

static void
receive_left_frame(struct xrt_frame_sink *sink, struct xrt_frame *xf)
{
	struct euroc_player *ep = container_of(sink, struct euroc_player, left_sink);
	EUROC_TRACE(ep, "left img t=%ld source_t=%ld", xf->timestamp, xf->source_timestamp);
	u_sink_debug_push_frame(&ep->ui_left_sink, xf);
	if (ep->out_sinks.left) {
		xrt_sink_push_frame(ep->out_sinks.left, xf);
	}
}

static void
receive_right_frame(struct xrt_frame_sink *sink, struct xrt_frame *xf)
{
	struct euroc_player *ep = container_of(sink, struct euroc_player, right_sink);
	EUROC_TRACE(ep, "right img t=%ld source_t=%ld", xf->timestamp, xf->source_timestamp);
	u_sink_debug_push_frame(&ep->ui_right_sink, xf);
	if (ep->out_sinks.right) {
		xrt_sink_push_frame(ep->out_sinks.right, xf);
	}
}

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
//! is heavily customizable, it will be managed through the UI. So this will not
//! really start outputting frames but mainly prepare everything to start doing
//! so when the user decides.
static bool
euroc_player_stream_start(struct xrt_fs *xfs,
                          struct xrt_frame_sink *xs,
                          enum xrt_fs_capture_type capture_type,
                          uint32_t descriptor_index)
{
	struct euroc_player *ep = euroc_player(xfs);

	if (xs == NULL && capture_type == XRT_FS_CAPTURE_TYPE_TRACKING) {
		EUROC_INFO(ep, "Starting Euroc Player in tracking mode");
		if (ep->out_sinks.left == NULL) {
			EUROC_WARN(ep, "No left sink provided, will keep running but tracking is unlikely to work");
		}
	} else if (xs != NULL && capture_type == XRT_FS_CAPTURE_TYPE_CALIBRATION) {
		EUROC_INFO(ep, "Starting Euroc Player in calibration mode, will stream only left frames right away");
		ep->out_sinks.left = xs;
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

	os_thread_helper_stop(&ep->play_thread);
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
	return;
}

static void
euroc_player_destroy(struct xrt_frame_node *node)
{
	struct euroc_player *ep = container_of(node, struct euroc_player, node);

	delete ep->imus;
	delete ep->left_imgs;
	delete ep->right_imgs;

	u_var_remove_root(ep);
	u_sink_debug_destroy(&ep->ui_left_sink);
	u_sink_debug_destroy(&ep->ui_right_sink);
	m_ff_vec3_f32_free(&ep->gyro_ff);
	m_ff_vec3_f32_free(&ep->accel_ff);

	free(ep);

	return;
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
	ret |= os_thread_helper_start(&ep->play_thread, euroc_player_mainloop, ep);
	EUROC_ASSERT(ret == 0, "Thread launch failure");

	euroc_player_set_ui_state(ep, STREAM_PLAYING);
}

static void
euroc_player_pause_btn_cb(void *ptr)
{
	struct euroc_player *ep = (struct euroc_player *)ptr;
	ep->playback.paused = !ep->playback.paused;
	euroc_player_set_ui_state(ep, ep->playback.paused ? STREAM_PAUSED : STREAM_PLAYING);
}

static void
euroc_player_setup_gui(struct euroc_player *ep)
{
	// Set sinks to display in UI
	u_sink_debug_init(&ep->ui_left_sink);
	u_sink_debug_init(&ep->ui_right_sink);
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
	u_var_add_ro_text(ep, "When using a SLAM system, setting these after start is unlikely to work", "Note");
	u_var_add_bool(ep, &ep->playback.stereo, "Stereo (if available)");
	u_var_add_bool(ep, &ep->playback.color, "Color (if available)");
	u_var_add_f32(ep, &ep->playback.skip_first_s, "First seconds to skip (set at start)");
	u_var_add_f32(ep, &ep->playback.scale, "Scale");
	u_var_add_f64(ep, &ep->playback.speed, "Speed (set at start)");
	u_var_add_bool(ep, &ep->playback.send_all_imus_first, "Send all IMU samples now");
	u_var_add_bool(ep, &ep->playback.use_source_ts, "Don't correct timestamps (set at start)");

	u_var_add_gui_header(ep, NULL, "Streams");
	u_var_add_ro_ff_vec3_f32(ep, ep->gyro_ff, "Gyroscope");
	u_var_add_ro_ff_vec3_f32(ep, ep->accel_ff, "Accelerometer");
	u_var_add_sink_debug(ep, &ep->ui_left_sink, "Left Camera");
	u_var_add_sink_debug(ep, &ep->ui_right_sink, "Right Camera");
}

// Euroc driver creation

struct xrt_fs *
euroc_player_create(struct xrt_frame_context *xfctx, const char *path)
{
	struct euroc_player *ep = U_TYPED_CALLOC(struct euroc_player);
	euroc_player_fill_dataset_info(ep, path);
	ep->mode = xrt_fs_mode{
	    ep->dataset.width,
	    ep->dataset.height,
	    ep->dataset.is_colored ? XRT_FORMAT_R8G8B8 : XRT_FORMAT_R8,
	    // Stereo euroc *is* supported, but we don't expose that through the
	    // xrt_fs interface as it will be managed through two different sinks.
	    XRT_STEREO_FORMAT_NONE,
	};

	// Using pointers to not mix std::vector with a C-compatible struct
	ep->imus = new imu_samples{};
	ep->left_imgs = new img_samples{};
	ep->right_imgs = new img_samples{};

	ep->playback.stereo = ep->dataset.is_stereo;
	ep->playback.color = ep->dataset.is_colored;
	ep->playback.skip_first_s = 0.0;
	ep->playback.scale = 1.0;
	ep->playback.speed = 1.0;
	ep->playback.send_all_imus_first = false;
	ep->playback.use_source_ts = false;

	ep->log_level = debug_get_log_option_euroc_log();
	euroc_player_setup_gui(ep);

	ep->left_sink.push_frame = receive_left_frame;
	ep->right_sink.push_frame = receive_right_frame;
	ep->imu_sink.push_imu = receive_imu_sample;
	ep->in_sinks.left = &ep->left_sink;
	ep->in_sinks.right = &ep->right_sink;
	ep->in_sinks.imu = &ep->imu_sink;
	ep->out_sinks = {0, 0, 0};

	struct xrt_fs *xfs = &ep->base;
	xfs->enumerate_modes = euroc_player_enumerate_modes;
	xfs->configure_capture = euroc_player_configure_capture;
	xfs->stream_start = euroc_player_stream_start;
	xfs->slam_stream_start = euroc_player_slam_stream_start;
	xfs->stream_stop = euroc_player_stream_stop;
	xfs->is_running = euroc_player_is_running;

	snprintf(xfs->name, sizeof(xfs->name), EUROC_PLAYER_STR);
	snprintf(xfs->product, sizeof(xfs->product), EUROC_PLAYER_STR " Product");
	snprintf(xfs->manufacturer, sizeof(xfs->manufacturer), EUROC_PLAYER_STR " Manufacturer");
	snprintf(xfs->serial, sizeof(xfs->serial), EUROC_PLAYER_STR " Serial");
	xfs->source_id = 0xECD0FEED;

	struct xrt_frame_node *xfn = &ep->node;
	xfn->break_apart = euroc_player_break_apart;
	xfn->destroy = euroc_player_destroy;

	xrt_frame_context_add(xfctx, &ep->node);

	EUROC_DEBUG(ep, "Euroc player created");

	return xfs;
}
