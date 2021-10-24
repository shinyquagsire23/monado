// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  RealSense device tracked with host-SLAM.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup drv_rs
 *
 * Originally created and tried on the D455 model but should work in any
 * RealSense device that has video and IMU streams.
 *
 * Be aware that you need to properly set the SLAM_CONFIG file to match
 * your camera specifics (stereo/mono, intrinsics, extrinsics, etc).
 */

#include "math/m_filter_fifo.h"
#include "math/m_space.h"
#include "os/os_time.h"
#include "util/u_device.h"
#include "util/u_logging.h"
#include "util/u_debug.h"
#include "util/u_var.h"
#include "util/u_sink.h"
#include "util/u_config_json.h"
#include "util/u_format.h"
#include "tracking/t_tracking.h"

#include "xrt/xrt_device.h"
#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_prober.h"
#include "xrt/xrt_config_have.h"

#include "rs_driver.h"

#include <librealsense2/rs.h>
#include <librealsense2/h/rs_pipeline.h>
#include <stdio.h>
#include <assert.h>

// These defaults come from a D455 camera, they might not work for other devices
#define DEFAULT_STEREO true
#define DEFAULT_XRT_VIDEO_FORMAT XRT_FORMAT_L8
#define DEFAULT_VIDEO_FORMAT RS2_FORMAT_Y8
#define DEFAULT_VIDEO_WIDTH 640
#define DEFAULT_VIDEO_HEIGHT 360
#define DEFAULT_VIDEO_FPS 30
#define DEFAULT_GYRO_FPS 200
#define DEFAULT_ACCEL_FPS 250
#define DEFAULT_STREAM_TYPE RS2_STREAM_INFRARED
#define DEFAULT_STREAM1_INDEX 1
#define DEFAULT_STREAM2_INDEX 2

#define RS_DEVICE_STR "Intel RealSense Host-SLAM"
#define RS_SOURCE_STR "RealSense Source"
#define RS_HOST_SLAM_TRACKER_STR "Host SLAM Tracker for RealSense"

#define RS_TRACE(r, ...) U_LOG_IFL_T(r->ll, __VA_ARGS__)
#define RS_DEBUG(r, ...) U_LOG_IFL_D(r->ll, __VA_ARGS__)
#define RS_INFO(r, ...) U_LOG_IFL_I(r->ll, __VA_ARGS__)
#define RS_WARN(r, ...) U_LOG_IFL_W(r->ll, __VA_ARGS__)
#define RS_ERROR(r, ...) U_LOG_IFL_E(r->ll, __VA_ARGS__)
#define RS_ASSERT(predicate, ...)                                                                                      \
	do {                                                                                                           \
		bool p = predicate;                                                                                    \
		if (!p) {                                                                                              \
			U_LOG(U_LOGGING_ERROR, __VA_ARGS__);                                                           \
			assert(false && "RS_ASSERT failed: " #predicate);                                              \
			exit(EXIT_FAILURE);                                                                            \
		}                                                                                                      \
	} while (false);
#define RS_ASSERT_(predicate) RS_ASSERT(predicate, "Assertion failed " #predicate)

// Debug assertions, not vital but useful for finding errors
#ifdef NDEBUG
#define RS_DASSERT(predicate, ...)
#define RS_DASSERT_(predicate)
#else
#define RS_DASSERT(predicate, ...) RS_ASSERT(predicate, __VA_ARGS__)
#define RS_DASSERT_(predicate) RS_ASSERT_(predicate)
#endif

//! Utility for realsense API calls that can produce errors
#define DO(call, ...)                                                                                                  \
	call(__VA_ARGS__, &rs->rsc.error_status);                                                                      \
	check_error(rs, rs->rsc.error_status, __FILE__, __LINE__)

//! Alternative to DO() with no arguments
#define DO_(call)                                                                                                      \
	call(&rs->rsc.error_status);                                                                                   \
	check_error(rs, rs->rsc.error_status, __FILE__, __LINE__)

//! @todo Use one RS_LOG option for the entire driver
DEBUG_GET_ONCE_LOG_OPTION(rs_log, "RS_HDEV_LOG", U_LOGGING_WARN)

// Forward declarations
static void
receive_left_frame(struct xrt_frame_sink *sink, struct xrt_frame *);
static void
receive_right_frame(struct xrt_frame_sink *sink, struct xrt_frame *);
static void
receive_imu_sample(struct xrt_imu_sink *sink, struct xrt_imu_sample *);
static void
rs_source_node_break_apart(struct xrt_frame_node *);
static void
rs_source_node_destroy(struct xrt_frame_node *);

/*!
 * Host-SLAM tracked RealSense device (any RealSense device with camera and IMU streams).
 *
 * @implements xrt_device
 */
struct rs_hdev
{
	struct xrt_device xdev;
	struct xrt_tracked_slam *slam;
	struct xrt_pose pose;    //!< Device pose
	struct xrt_pose offset;  //!< Additional offset to apply to `pose`
	enum u_logging_level ll; //!< Log level
};

/*!
 * RealSense source of camera and IMU data.
 *
 * @implements xrt_fs
 * @implements xrt_frame_node
 */
struct rs_source
{
	struct xrt_fs xfs;
	struct xrt_frame_node node;
	enum u_logging_level ll; //!< Log level

	// Sinks
	struct xrt_frame_sink left_sink;  //!< Intermediate sink for left camera frames
	struct xrt_frame_sink right_sink; //!< Intermediate sink for right camera frames
	struct xrt_imu_sink imu_sink;     //!< Intermediate sink for IMU samples
	struct xrt_slam_sinks in_sinks;   //!< Pointers to intermediate sinks
	struct xrt_slam_sinks out_sinks;  //!< Pointers to downstream sinks

	// UI Sinks
	struct u_sink_debug ui_left_sink;  //!< Sink to display left frames in UI
	struct u_sink_debug ui_right_sink; //!< Sink to display right frames in UI
	struct m_ff_vec3_f32 *gyro_ff;     //!< Queue of gyroscope data to display in UI
	struct m_ff_vec3_f32 *accel_ff;    //!< Queue of accelerometer data to display in UI

	struct rs_container rsc; //!< Container of RealSense API objects

	// Properties loaded from json file and used when configuring the realsense pipeline
	bool stereo;                      //!< Indicates whether to use one or two cameras
	rs2_format video_format;          //!< Indicates desired frame color format
	enum xrt_format xrt_video_format; //!< corresponding format for video_format
	int video_width;                  //!< Indicates desired frame width
	int video_height;                 //!< Indicates desired frame height
	int video_fps;                    //!< Indicates desired fps
	int gyro_fps;                     //!< Indicates desired gyroscope samples per second
	int accel_fps;                    //!< Indicates desired accelerometer samples per second
	rs2_stream stream_type;           //!< Indicates desired stream type for the cameras
	int stream1_index;                //!< Indicates desired stream index for first stream
	int stream2_index;                //!< Indicates desired stream index for second stream

	bool is_running; //!< Whether the device is streaming

	//! Very simple struct to merge the two acc/gyr streams into one IMU stream.
	//! It just pushes on every gyro sample and reuses the latest acc sample.
	struct
	{
		struct os_mutex mutex; //!< Gyro and accel come from separate threads
		struct xrt_vec3 accel; //!< Last received accelerometer values
		struct xrt_vec3 gyro;  //!< Last received gyroscope values
	} partial_imu_sample;
};

//! @todo Unify check_error() and DO() usage thorough the driver.
static bool
check_error(struct rs_source *rs, rs2_error *e, const char *file, int line)
{
	if (e == NULL) {
		return false; // No errors
	}

	RS_ERROR(rs, "rs_error was raised when calling %s(%s):", rs2_get_failed_function(e), rs2_get_failed_args(e));
	RS_ERROR(rs, "%s:%d: %s", file, line, rs2_get_error_message(e));
	exit(EXIT_FAILURE);
}


/*
 *
 * Device functionality
 *
 */

static inline struct rs_hdev *
rs_hdev_from_xdev(struct xrt_device *xdev)
{
	struct rs_hdev *rh = container_of(xdev, struct rs_hdev, xdev);
	return rh;
}

static void
rs_hdev_update_inputs(struct xrt_device *xdev)
{
	return;
}

//! Specific pose corrections for Kimera and the D455 camera
XRT_MAYBE_UNUSED static inline struct xrt_pose
rs_hdev_correct_pose_from_kimera(struct xrt_pose pose)
{
	// Correct swapped axes
	struct xrt_pose swapped = {0};
	swapped.position.x = -pose.position.y;
	swapped.position.y = -pose.position.z;
	swapped.position.z = pose.position.x;
	swapped.orientation.x = -pose.orientation.y;
	swapped.orientation.y = -pose.orientation.z;
	swapped.orientation.z = pose.orientation.x;
	swapped.orientation.w = pose.orientation.w;

	// Correct orientation
	//! @todo Encode this transformation into constants
	struct xrt_space_relation out_relation;
	struct xrt_space_graph space_graph = {0};
	struct xrt_pose pre_correction = {{-0.5, -0.5, -0.5, 0.5}, {0, 0, 0}}; // euler(90, 90, 0)
	float sin45 = 0.7071067811865475;
	struct xrt_pose pos_correction = {{sin45, 0, sin45, 0}, {0, 0, 0}}; // euler(180, 90, 0)
	m_space_graph_add_pose(&space_graph, &pre_correction);
	m_space_graph_add_pose(&space_graph, &swapped);
	m_space_graph_add_pose(&space_graph, &pos_correction);
	m_space_graph_resolve(&space_graph, &out_relation);
	return out_relation.pose;
}

//! Specific pose corrections for Basalt and the D455 camera
XRT_MAYBE_UNUSED static inline struct xrt_pose
rs_hdev_correct_pose_from_basalt(struct xrt_pose pose)
{
	// Correct swapped axes
	struct xrt_pose swapped = {0};
	swapped.position.x = pose.position.x;
	swapped.position.y = -pose.position.y;
	swapped.position.z = -pose.position.z;
	swapped.orientation.x = pose.orientation.x;
	swapped.orientation.y = -pose.orientation.y;
	swapped.orientation.z = -pose.orientation.z;
	swapped.orientation.w = pose.orientation.w;

	// Correct orientation
	//! @todo Encode this transformation into constants
	struct xrt_space_relation out_relation;
	struct xrt_space_graph space_graph = {0};
	const float sin45 = 0.7071067811865475;
	struct xrt_pose pos_correction = {{sin45, 0, 0, sin45}, {0, 0, 0}}; // euler(90, 0, 0)

	m_space_graph_add_pose(&space_graph, &swapped);
	m_space_graph_add_pose(&space_graph, &pos_correction);
	m_space_graph_resolve(&space_graph, &out_relation);
	return out_relation.pose;
}

static void
rs_hdev_get_tracked_pose(struct xrt_device *xdev,
                         enum xrt_input_name name,
                         uint64_t at_timestamp_ns,
                         struct xrt_space_relation *out_relation)
{
	struct rs_hdev *rh = rs_hdev_from_xdev(xdev);
	RS_ASSERT_(rh->slam != NULL);
	RS_ASSERT_(at_timestamp_ns < INT64_MAX);

	xrt_tracked_slam_get_tracked_pose(rh->slam, at_timestamp_ns, out_relation);

	int pose_bits = XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
	bool pose_tracked = out_relation->relation_flags & pose_bits;

	if (pose_tracked) {
#if defined(XRT_HAVE_KIMERA_SLAM)
		rh->pose = rs_hdev_correct_pose_from_kimera(out_relation->pose);
#elif defined(XRT_HAVE_BASALT_SLAM)
		rh->pose = rs_hdev_correct_pose_from_basalt(out_relation->pose);
#else
		rh->pose = out_relation->pose;
#endif
	}

	struct xrt_space_graph space_graph = {0};
	m_space_graph_add_pose(&space_graph, &rh->pose);
	m_space_graph_add_pose(&space_graph, &rh->offset);
	m_space_graph_resolve(&space_graph, out_relation);
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
}

static void
rs_hdev_destroy(struct xrt_device *xdev)
{
	struct rs_hdev *rh = rs_hdev_from_xdev(xdev);
	RS_INFO(rh, "Destroying rs_hdev");
	u_var_remove_root(rh);
	u_device_free(&rh->xdev);
}


/*
 *
 * JSON functionality
 *
 */

#define JSON_CONFIG_FIELD_NAME "config_realsense_hdev"

//! Helper function for loading an int field from a json container and printing useful
//! messages along it. *out is expected to come preloaded with a default value.
static void
json_int(struct rs_source *rs, const cJSON *json, const char *field, int *out)
{
	if (!u_json_get_int(u_json_get(json, field), out)) {
		// This is a warning because we want the user to set these config fields
		RS_WARN(rs, "Using default %s.%s=%d", JSON_CONFIG_FIELD_NAME, field, *out);
	} else {
		RS_DEBUG(rs, "Using %s.%s=%d", JSON_CONFIG_FIELD_NAME, field, *out);
	}
}

//! Similar to @ref json_int but for bools.
static void
json_bool(struct rs_source *rs, const cJSON *json, const char *field, bool *out)
{
	if (!u_json_get_bool(u_json_get(json, field), out)) {
		// This is a warning because we want the user to set these config fields
		RS_WARN(rs, "Using default %s.%s=%s", JSON_CONFIG_FIELD_NAME, field, *out ? "true" : "false");
	} else {
		RS_DEBUG(rs, "Using %s.%s=%s", JSON_CONFIG_FIELD_NAME, field, *out ? "true" : "false");
	}
}

//! Similar to @ref json_int but for a video rs2_format, also sets the
//! equivalent xrt_format if any.
static void
json_rs2_format(
    struct rs_source *rs, const cJSON *json, const char *field, rs2_format *out_rformat, enum xrt_format *out_xformat)
{
	int format_int = (int)*out_rformat;
	bool valid_field = u_json_get_int(u_json_get(json, field), &format_int);
	if (!valid_field) {
		RS_WARN(rs, "Using default %s.%s=%d (%d)", JSON_CONFIG_FIELD_NAME, field, *out_rformat, *out_xformat);
		return;
	}

	rs2_format rformat = (rs2_format)format_int;
	enum xrt_format xformat;
	if (rformat == RS2_FORMAT_Y8) {
		xformat = XRT_FORMAT_L8;
	} else if (rformat == RS2_FORMAT_RGB8 || rformat == RS2_FORMAT_BGR8) {
		xformat = XRT_FORMAT_R8G8B8;
	} else {
		RS_ERROR(rs, "Invalid %s.%s=%d", JSON_CONFIG_FIELD_NAME, field, format_int);
		RS_ERROR(rs, "Valid values: %d, %d, %d", RS2_FORMAT_Y8, RS2_FORMAT_RGB8, RS2_FORMAT_BGR8);
		RS_ERROR(rs, "Using default %s.%s=%d (%d)", JSON_CONFIG_FIELD_NAME, field, *out_rformat, *out_xformat);

		// Reaching this doesn't mean that a matching xrt_format doesn't exist, just
		// that I didn't need it. Feel free to add it.

		return;
	}

	*out_rformat = rformat;
	*out_xformat = xformat;
	RS_DEBUG(rs, "Using %s.%s=%d (xrt_format=%d)", JSON_CONFIG_FIELD_NAME, field, *out_rformat, *out_xformat);
}

//! Similar to @ref json_int but for a rs2_stream type.
static void
json_rs2_stream(struct rs_source *rs, const cJSON *json, const char *field, rs2_stream *out_stream)
{
	int stream_int = (int)*out_stream;
	bool valid_field = u_json_get_int(u_json_get(json, field), &stream_int);
	if (!valid_field) {
		RS_WARN(rs, "Using default %s.%s=%d", JSON_CONFIG_FIELD_NAME, field, *out_stream);
		return;
	}

	rs2_stream rstream = (rs2_stream)stream_int;
	if (rstream != RS2_STREAM_COLOR && rstream != RS2_STREAM_INFRARED && rstream != RS2_STREAM_FISHEYE) {
		RS_ERROR(rs, "Invalid %s.%s=%d", JSON_CONFIG_FIELD_NAME, field, stream_int);
		RS_ERROR(rs, "Valid values: %d, %d, %d", RS2_STREAM_COLOR, RS2_STREAM_INFRARED, RS2_STREAM_FISHEYE);
		RS_ERROR(rs, "Using default %s.%s=%d", JSON_CONFIG_FIELD_NAME, field, *out_stream);
		return;
	}

	*out_stream = rstream;
	RS_DEBUG(rs, "Using %s.%s=%d", JSON_CONFIG_FIELD_NAME, field, *out_stream);
}

static void
rs_source_load_stream_options_from_json(struct rs_source *rs)
{
	// Set default values
	rs->stereo = DEFAULT_STEREO;
	rs->xrt_video_format = DEFAULT_XRT_VIDEO_FORMAT;
	rs->video_format = DEFAULT_VIDEO_FORMAT;
	rs->video_width = DEFAULT_VIDEO_WIDTH;
	rs->video_height = DEFAULT_VIDEO_HEIGHT;
	rs->video_fps = DEFAULT_VIDEO_FPS;
	rs->gyro_fps = DEFAULT_GYRO_FPS;
	rs->accel_fps = DEFAULT_ACCEL_FPS;
	rs->stream_type = DEFAULT_STREAM_TYPE;
	rs->stream1_index = DEFAULT_STREAM1_INDEX;
	rs->stream2_index = DEFAULT_STREAM2_INDEX;

	struct u_config_json config = {0};
	u_config_json_open_or_create_main_file(&config);
	if (!config.file_loaded) {
		RS_WARN(rs, "Unable to load config file, will use default stream values");
		cJSON_Delete(config.root);
		return;
	}

	const cJSON *hdev_config = u_json_get(config.root, JSON_CONFIG_FIELD_NAME);
	if (hdev_config == NULL) {
		RS_WARN(rs, "Field '%s' not present in json file, will use defaults", JSON_CONFIG_FIELD_NAME);
	}

	json_bool(rs, hdev_config, "stereo", &rs->stereo);
	json_rs2_format(rs, hdev_config, "video_format", &rs->video_format, &rs->xrt_video_format);
	json_int(rs, hdev_config, "video_width", &rs->video_width);
	json_int(rs, hdev_config, "video_height", &rs->video_height);
	json_int(rs, hdev_config, "video_fps", &rs->video_fps);
	json_int(rs, hdev_config, "gyro_fps", &rs->gyro_fps);
	json_int(rs, hdev_config, "accel_fps", &rs->accel_fps);
	json_rs2_stream(rs, hdev_config, "stream_type", &rs->stream_type);
	json_int(rs, hdev_config, "stream1_index", &rs->stream1_index);
	json_int(rs, hdev_config, "stream2_index", &rs->stream2_index);

	cJSON_Delete(config.root);
}


/*
 *
 * Realsense functionality
 *
 */

//! Disable any laser emitters because they confuse SLAM feature detection
static void
disable_all_laser_emitters(struct rs_source *rs)
{
	struct rs_container *rsc = &rs->rsc;
	rs2_sensor_list *sensors = DO(rs2_query_sensors, rsc->device);
	int sensors_count = DO(rs2_get_sensors_count, sensors);
	for (int i = 0; i < sensors_count; i++) {
		rs2_sensor *sensor = DO(rs2_create_sensor, sensors, i);
		rs2_options *sensor_options = (rs2_options *)sensor;
		bool has_emitter = DO(rs2_supports_option, sensor_options, RS2_OPTION_EMITTER_ENABLED);
		if (has_emitter) {
			DO(rs2_set_option, sensor_options, RS2_OPTION_EMITTER_ENABLED, 0);
		}
		rs2_delete_sensor(sensor);
	}
	rs2_delete_sensor_list(sensors);
}


/*
 *
 * Stream functionality
 *
 */

static void
rs_source_frame_destroy(struct xrt_frame *xf)
{
	rs2_frame *rframe = (rs2_frame *)xf->owner;
	rs2_release_frame(rframe);
	free(xf);
}

static void
rs2xrt_frame(struct rs_source *rs, rs2_frame *rframe, struct xrt_frame **out_xframe)
{
	RS_ASSERT_(*out_xframe == NULL);

	uint64_t number = DO(rs2_get_frame_number, rframe);
	double timestamp_ms = DO(rs2_get_frame_timestamp, rframe);
	uint8_t *data = (uint8_t *)DO(rs2_get_frame_data, rframe);
	int bytes_per_pixel = u_format_block_size(rs->xrt_video_format);
	int stride = rs->video_width * bytes_per_pixel;

#ifndef NDEBUG // Debug only: check that the realsense stream is behaving as expected
	bool is_video_frame = DO(rs2_is_frame_extendable_to, rframe, RS2_EXTENSION_VIDEO_FRAME);
	int rs_bits_per_pixel = DO(rs2_get_frame_bits_per_pixel, rframe);
	int rs_width = DO(rs2_get_frame_width, rframe);
	int rs_height = DO(rs2_get_frame_height, rframe);
	int rs_stride = DO(rs2_get_frame_stride_in_bytes, rframe);
	RS_DASSERT_(is_video_frame);
	RS_DASSERT_(rs_bits_per_pixel == bytes_per_pixel * 8);
	RS_DASSERT(rs_width == rs->video_width, "%d != %d", rs_width, rs->video_width);
	RS_DASSERT(rs_height == rs->video_height, "%d != %d", rs_height, rs->video_height);
	RS_DASSERT(rs_stride == stride, "%d != %d", rs_stride, stride);
#endif

	struct xrt_frame *xf = U_TYPED_CALLOC(struct xrt_frame);
	xf->reference.count = 1;
	xf->destroy = rs_source_frame_destroy;
	xf->owner = rframe;
	xf->width = rs->video_width;
	xf->height = rs->video_height;
	xf->stride = stride;
	xf->size = rs->video_height * stride;
	xf->data = data;

	xf->format = rs->xrt_video_format;
	xf->stereo_format = XRT_STEREO_FORMAT_NONE; //!< @todo Use a stereo xrt_format

	uint64_t timestamp_ns = timestamp_ms * 1000 * 1000;
	xf->timestamp = timestamp_ns;
	xf->source_timestamp = timestamp_ns;
	xf->source_sequence = number;
	xf->source_id = rs->xfs.source_id;

	*out_xframe = xf;
}

static void
handle_frameset(struct rs_source *rs, rs2_frame *frames)
{

	// Check number of frames on debug builds
	int num_of_frames = DO(rs2_embedded_frames_count, frames);
	if (rs->stereo) {
		RS_DASSERT(num_of_frames == 2, "Stereo frameset contains %d (!= 2) frames", num_of_frames);
	} else {
		RS_DASSERT(num_of_frames == 1, "Non-stereo frameset contains %d (!= 1) frames", num_of_frames);
	}

	// Left frame
	rs2_frame *rframe_left = DO(rs2_extract_frame, frames, 0);
	struct xrt_frame *xf_left = NULL;
	rs2xrt_frame(rs, rframe_left, &xf_left);

	if (rs->stereo) {

		// Right frame
		rs2_frame *rframe_right = DO(rs2_extract_frame, frames, 1);
		struct xrt_frame *xf_right = NULL;
		rs2xrt_frame(rs, rframe_right, &xf_right);

		if (xf_left->timestamp == xf_right->timestamp) {
			xrt_sink_push_frame(rs->in_sinks.left, xf_left);
			xrt_sink_push_frame(rs->in_sinks.right, xf_right);
		} else {
			// This usually happens only once at start and never again
			RS_WARN(rs, "Realsense device sent left and right frames with different timestamps %ld != %ld",
			        xf_left->timestamp, xf_right->timestamp);
		}

		xrt_frame_reference(&xf_right, NULL);
	} else {
		xrt_sink_push_frame(rs->in_sinks.left, xf_left);
	}

	xrt_frame_reference(&xf_left, NULL);

	// Release frameset but individual frames will be released on xrt_frame destruction
	rs2_release_frame(frames);
}

//! Decides when to submit the full IMU sample out of separate
//! gyroscope/accelerometer samples.
static void
partial_imu_sample_push(struct rs_source *rs, timepoint_ns ts, struct xrt_vec3 vals, bool is_gyro)
{
	os_mutex_lock(&rs->partial_imu_sample.mutex);

	if (is_gyro) {
		rs->partial_imu_sample.gyro = vals;
	} else {
		rs->partial_imu_sample.accel = vals;
	}
	struct xrt_vec3 gyro = rs->partial_imu_sample.gyro;
	struct xrt_vec3 accel = rs->partial_imu_sample.accel;

	// Push IMU sample from fastest motion sensor arrives, reuse latest data from the other sensor (or zero)
	bool should_submit = (rs->gyro_fps > rs->accel_fps) == is_gyro;
	if (should_submit) {
		struct xrt_imu_sample sample = {ts, {accel.x, accel.y, accel.z}, {gyro.x, gyro.y, gyro.z}};
		xrt_sink_push_imu(rs->in_sinks.imu, &sample);
	}

	os_mutex_unlock(&rs->partial_imu_sample.mutex);
}

static void
handle_gyro_frame(struct rs_source *rs, rs2_frame *frame)
{
	const float *data = DO(rs2_get_frame_data, frame);

#ifndef NDEBUG
	int data_size = DO(rs2_get_frame_data_size, frame);
	RS_DASSERT(data_size == 3 * sizeof(float) || data_size == 4 * sizeof(float), "Unexpected size=%d", data_size);
	RS_DASSERT_(data_size != 4 || data[3] == 0);
#endif

	double timestamp_ms = DO(rs2_get_frame_timestamp, frame);
	timepoint_ns timestamp_ns = timestamp_ms * 1000 * 1000;
	struct xrt_vec3 gyro = {data[0], data[1], data[2]};
	RS_TRACE(rs, "gyro t=%ld x=%f y=%f z=%f", timestamp_ns, gyro.x, gyro.y, gyro.z);
	partial_imu_sample_push(rs, timestamp_ns, gyro, true);
	rs2_release_frame(frame);
}

static void
handle_accel_frame(struct rs_source *rs, rs2_frame *frame)
{
	const float *data = DO(rs2_get_frame_data, frame);

#ifndef NDEBUG
	int data_size = DO(rs2_get_frame_data_size, frame);
	// For some strange reason data_size is 4 for samples that can use hardware
	// timestamps. And that last element data[3] seems to always be zero.
	RS_DASSERT(data_size == 3 * sizeof(float) || data_size == 4 * sizeof(float), "Unexpected size=%d", data_size);
	RS_DASSERT_(data_size != 4 || data[3] == 0);
#endif

	double timestamp_ms = DO(rs2_get_frame_timestamp, frame);
	timepoint_ns timestamp_ns = timestamp_ms * 1000 * 1000;
	struct xrt_vec3 accel = {data[0], data[1], data[2]};
	RS_TRACE(rs, "accel t=%ld x=%f y=%f z=%f", timestamp_ns, accel.x, accel.y, accel.z);
	partial_imu_sample_push(rs, timestamp_ns, accel, false);
	rs2_release_frame(frame);
}

//! Checks that the timestamp domain of the realsense sample (the frame) is in
//! global time or, at the very least, in another domain that we support.
static inline void
check_global_time(struct rs_source *rs, rs2_frame *frame, rs2_stream stream_type)
{

#ifndef NDEBUG // Check valid timestamp domains only on debug builds
	rs2_timestamp_domain ts_domain = DO(rs2_get_frame_timestamp_domain, frame);
	bool using_global_time = ts_domain == RS2_TIMESTAMP_DOMAIN_GLOBAL_TIME;
	bool acceptable_timestamp_domain = using_global_time;

	//! @note We should be ensuring that we have the same timestamp domains in all
	//! sensors. But the user might have a newer kernel versions that is not
	//! supported by the RealSense DKMS package that allows GLOBAL_TIME for all
	//! sensors. From my experience and based on other users' reports, the only
	//! affected sensor without GLOBAL_TIME is the gyroscope, which is ~30ms off.
	//! See https://github.com/IntelRealSense/librealsense/issues/5710

	bool is_accel = stream_type == RS2_STREAM_ACCEL;
	bool is_gyro = stream_type == RS2_STREAM_GYRO;
	bool is_motion_sensor = is_accel || is_gyro;

	if (is_motion_sensor) {
		bool is_gyro_slower = rs->gyro_fps < rs->accel_fps;
		bool is_slower_motion_sensor = is_gyro_slower == is_gyro;

		// We allow different domains for the slower sensor because partial_imu_sample
		// discards those timestamps
		acceptable_timestamp_domain |= is_slower_motion_sensor;
	}

	if (!acceptable_timestamp_domain) {
		RS_ERROR(rs, "Invalid ts_domain=%s", rs2_timestamp_domain_to_string(ts_domain));
		RS_ERROR(rs, "One of your RealSense sensors is not using GLOBAL_TIME domain for its timestamps.");
		RS_ERROR(rs, "This should be solved by applying the kernel patch required by the RealSense SDK.");
		if (is_motion_sensor) {
			const char *a = is_accel ? "accelerometer" : "gyroscope";
			const char *b = is_accel ? "gyroscope" : "accelerometer";
			RS_ERROR(rs, "As an alternative, set %s frequency to be greater than %s frequency.", b, a);
		}
		RS_DASSERT(false, "Unacceptable timestamp domain %s", rs2_timestamp_domain_to_string(ts_domain));
	}
#endif
}

static void
on_frame(rs2_frame *frame, void *ptr)
{
	struct rs_source *rs = (struct rs_source *)ptr;

	const rs2_stream_profile *stream = DO(rs2_get_frame_stream_profile, frame);
	rs2_stream stream_type;
	rs2_format format;
	int index, unique_id, framerate;
	DO(rs2_get_stream_profile_data, stream, &stream_type, &format, &index, &unique_id, &framerate);

	bool is_frameset = DO(rs2_is_frame_extendable_to, frame, RS2_EXTENSION_COMPOSITE_FRAME);
	bool is_motion_frame = DO(rs2_is_frame_extendable_to, frame, RS2_EXTENSION_MOTION_FRAME);
	check_global_time(rs, frame, stream_type);

	if (stream_type == rs->stream_type) {
		RS_DASSERT_(is_frameset && format == rs->video_format &&
		            (index == rs->stream1_index || index == rs->stream2_index) && framerate == rs->video_fps);
		handle_frameset(rs, frame);
	} else if (stream_type == RS2_STREAM_GYRO) {
		RS_DASSERT_(is_motion_frame && format == RS2_FORMAT_MOTION_XYZ32F && framerate == rs->gyro_fps);
		handle_gyro_frame(rs, frame);
	} else if (stream_type == RS2_STREAM_ACCEL) {
		RS_DASSERT_(is_motion_frame && format == RS2_FORMAT_MOTION_XYZ32F && framerate == rs->accel_fps);
		handle_accel_frame(rs, frame);
	} else {
		RS_ASSERT(false, "Unexpected stream");
	}
}


/*
 *
 * Frameserver functionality
 *
 */

static inline struct rs_source *
rs_source_from_xfs(struct xrt_fs *xfs)
{
	struct rs_source *rs = container_of(xfs, struct rs_source, xfs);
	return rs;
}

static bool
rs_source_enumerate_modes(struct xrt_fs *xfs, struct xrt_fs_mode **out_modes, uint32_t *out_count)
{
	struct rs_source *rs = container_of(xfs, struct rs_source, xfs);
	struct xrt_fs_mode *modes = U_TYPED_ARRAY_CALLOC(struct xrt_fs_mode, 1);
	RS_ASSERT(modes != NULL, "Unable to calloc rs_source playback modes");

	//! @todo only exposing the one stream configuration the user provided through
	//! the json configuration but we could show all possible stream setups.
	modes[0] = (struct xrt_fs_mode){.width = rs->video_width,
	                                .height = rs->video_height,
	                                .format = rs->xrt_video_format,
	                                //! @todo The stereo_format being NONE is incorrect but one that supports
	                                //! frames in different memory regions does not exist yet.
	                                .stereo_format = XRT_STEREO_FORMAT_NONE};

	*out_modes = modes;
	*out_count = 1;

	return true;
}

static bool
rs_source_configure_capture(struct xrt_fs *xfs, struct xrt_fs_capture_parameters *cp)
{
	//! @todo implement
	RS_ASSERT(false, "Not Implemented");
	return false;
}

static bool
rs_source_stream_stop(struct xrt_fs *xfs)
{
	struct rs_source *rs = rs_source_from_xfs(xfs);
	if (rs->is_running) {
		DO(rs2_pipeline_stop, rs->rsc.pipeline);
		rs->is_running = false;
	}
	return true;
}

static bool
rs_source_is_running(struct xrt_fs *xfs)
{
	struct rs_source *rs = rs_source_from_xfs(xfs);
	return rs->is_running;
}

static bool
rs_source_stream_start(struct xrt_fs *xfs,
                       struct xrt_frame_sink *xs,
                       enum xrt_fs_capture_type capture_type,
                       uint32_t descriptor_index)
{
	struct rs_source *rs = rs_source_from_xfs(xfs);
	if (xs == NULL && capture_type == XRT_FS_CAPTURE_TYPE_TRACKING) {
		RS_ASSERT(rs->out_sinks.left != NULL, "No left sink provided");
		RS_INFO(rs, "Starting RealSense stream in tracking mode");
	} else if (xs != NULL && capture_type == XRT_FS_CAPTURE_TYPE_CALIBRATION) {
		RS_INFO(rs, "Starting RealSense stream in calibration mode, will stream only left frames");
		rs->out_sinks.left = xs;
	} else {
		RS_ASSERT(false, "Unsupported stream configuration xs=%p capture_type=%d", (void *)xs, capture_type);
		return false;
	}

	struct rs_container *rsc = &rs->rsc;
	rsc->profile = DO(rs2_pipeline_start_with_config_and_callback, rsc->pipeline, rsc->config, on_frame, rs);

	disable_all_laser_emitters(rs);

	rs->is_running = true;
	return rs->is_running;
}

static bool
rs_source_slam_stream_start(struct xrt_fs *xfs, struct xrt_slam_sinks *sinks)
{
	struct rs_source *rs = rs_source_from_xfs(xfs);
	rs->out_sinks = *sinks;
	return rs_source_stream_start(xfs, NULL, XRT_FS_CAPTURE_TYPE_TRACKING, 0);
}


/*
 *
 * Sinks functionality
 *
 */

static void
receive_left_frame(struct xrt_frame_sink *sink, struct xrt_frame *xf)
{
	struct rs_source *rs = container_of(sink, struct rs_source, left_sink);
	RS_TRACE(rs, "left img t=%ld source_t=%ld", xf->timestamp, xf->source_timestamp);
	u_sink_debug_push_frame(&rs->ui_left_sink, xf);
	if (rs->out_sinks.left) {
		xrt_sink_push_frame(rs->out_sinks.left, xf);
	}
}

static void
receive_right_frame(struct xrt_frame_sink *sink, struct xrt_frame *xf)
{
	struct rs_source *rs = container_of(sink, struct rs_source, right_sink);
	RS_TRACE(rs, "right img t=%ld source_t=%ld", xf->timestamp, xf->source_timestamp);
	u_sink_debug_push_frame(&rs->ui_right_sink, xf);
	if (rs->out_sinks.right) {
		xrt_sink_push_frame(rs->out_sinks.right, xf);
	}
}

static void
receive_imu_sample(struct xrt_imu_sink *sink, struct xrt_imu_sample *s)
{
	struct rs_source *rs = container_of(sink, struct rs_source, imu_sink);

	timepoint_ns ts = s->timestamp_ns;
	struct xrt_vec3_f64 a = s->accel_m_s2;
	struct xrt_vec3_f64 w = s->gyro_rad_secs;
	RS_TRACE(rs, "imu t=%ld a=(%f %f %f) w=(%f %f %f)", ts, a.x, a.y, a.z, w.x, w.y, w.z);

	// Push to debug UI by adjusting the timestamp to monotonic time

	struct xrt_vec3 gyro = {(float)w.x, (float)w.y, (float)w.z};
	struct xrt_vec3 accel = {(float)a.x, (float)a.y, (float)a.z};
	uint64_t now_realtime = os_realtime_get_ns();
	uint64_t now_monotonic = os_monotonic_get_ns();
	RS_DASSERT_(now_realtime < INT64_MAX);

	// Assertion commented because GLOBAL_TIME makes ts be a bit in the future
	// RS_DASSERT_(now_realtime < INT64_MAX && (timepoint_ns)now_realtime > ts);

	uint64_t imu_monotonic = now_monotonic - (now_realtime - ts);
	m_ff_vec3_f32_push(rs->gyro_ff, &gyro, imu_monotonic);
	m_ff_vec3_f32_push(rs->accel_ff, &accel, imu_monotonic);

	if (rs->out_sinks.imu) {
		xrt_sink_push_imu(rs->out_sinks.imu, s);
	}
}


/*
 *
 * Frame node functionality
 *
 */

static void
rs_source_node_break_apart(struct xrt_frame_node *node)
{
	struct rs_source *rs = container_of(node, struct rs_source, node);
	rs_source_stream_stop(&rs->xfs);
}

static void
rs_source_node_destroy(struct xrt_frame_node *node)
{
	struct rs_source *rs = container_of(node, struct rs_source, node);
	RS_INFO(rs, "Destroying RealSense source");
	os_mutex_destroy(&rs->partial_imu_sample.mutex);
	u_var_remove_root(rs);
	u_sink_debug_destroy(&rs->ui_left_sink);
	u_sink_debug_destroy(&rs->ui_right_sink);
	m_ff_vec3_f32_free(&rs->gyro_ff);
	m_ff_vec3_f32_free(&rs->accel_ff);
	rs_container_cleanup(&rs->rsc);
	free(rs);
}


/*
 *
 * Exported functions
 *
 */

struct xrt_device *
rs_hdev_create(struct xrt_prober *xp, int device_idx)
{
	struct rs_hdev *rh = U_DEVICE_ALLOCATE(struct rs_hdev, U_DEVICE_ALLOC_TRACKING_NONE, 1, 0);
	rh->ll = debug_get_log_option_rs_log();
	rh->pose = (struct xrt_pose){{0, 0, 0, 1}, {0, 0, 0}};
	rh->offset = (struct xrt_pose){{0, 0, 0, 1}, {0, 0, 0}};

	struct xrt_device *xd = &rh->xdev;
	xd->name = XRT_DEVICE_REALSENSE;
	xd->device_type = XRT_DEVICE_TYPE_GENERIC_TRACKER;

	snprintf(xd->str, XRT_DEVICE_NAME_LEN, "%s", RS_DEVICE_STR);
	snprintf(xd->serial, XRT_DEVICE_NAME_LEN, "%s", RS_DEVICE_STR);

	snprintf(xd->tracking_origin->name, XRT_TRACKING_NAME_LEN, "%s", RS_HOST_SLAM_TRACKER_STR);
	xd->tracking_origin->type = XRT_TRACKING_TYPE_EXTERNAL_SLAM;

	xd->inputs[0].name = XRT_INPUT_GENERIC_TRACKER_POSE;

	xd->orientation_tracking_supported = true;
	xd->position_tracking_supported = true;

	xd->update_inputs = rs_hdev_update_inputs;
	xd->get_tracked_pose = rs_hdev_get_tracked_pose;
	xd->destroy = rs_hdev_destroy;

	// Setup UI
	u_var_add_root(rh, "RealSense Device", false);
	u_var_add_ro_text(rh, "Host SLAM", "Tracked by");
	u_var_add_log_level(rh, &rh->ll, "Log Level");
	u_var_add_pose(rh, &rh->pose, "SLAM Pose");
	u_var_add_pose(rh, &rh->offset, "Offset Pose");

	bool tracked = xp->tracking->create_tracked_slam(xp->tracking, xd, &rh->slam) >= 0;
	if (!tracked) {
		RS_WARN(rh, "Unable to setup the SLAM tracker");
		rs_hdev_destroy(xd);
		return NULL;
	}

	RS_DEBUG(rh, "Host-SLAM RealSense device created");

	return xd;
}

//! Create and open the frame server for IMU/camera streaming.
struct xrt_fs *
rs_source_create(struct xrt_frame_context *xfctx, int device_idx)
{
	struct rs_source *rs = U_TYPED_CALLOC(struct rs_source);
	rs->ll = debug_get_log_option_rs_log();

	// Setup xrt_fs
	struct xrt_fs *xfs = &rs->xfs;
	xfs->enumerate_modes = rs_source_enumerate_modes;
	xfs->configure_capture = rs_source_configure_capture;
	xfs->stream_start = rs_source_stream_start;
	xfs->slam_stream_start = rs_source_slam_stream_start;
	xfs->stream_stop = rs_source_stream_stop;
	xfs->is_running = rs_source_is_running;
	snprintf(xfs->name, sizeof(xfs->name), RS_SOURCE_STR);
	snprintf(xfs->product, sizeof(xfs->product), RS_SOURCE_STR " Product");
	snprintf(xfs->manufacturer, sizeof(xfs->manufacturer), RS_SOURCE_STR " Manufacturer");
	snprintf(xfs->serial, sizeof(xfs->serial), RS_SOURCE_STR " Serial");
	xfs->source_id = 0x2EA15E115E;

	// Setup realsense pipeline data
	struct rs_container *rsc = &rs->rsc;
	rsc->error_status = NULL;
	rsc->context = DO(rs2_create_context, RS2_API_VERSION);
	rsc->device_list = DO(rs2_query_devices, rsc->context);
	rsc->device_count = DO(rs2_get_device_count, rsc->device_list);
	rsc->device_idx = device_idx;
	rsc->device = DO(rs2_create_device, rsc->device_list, rsc->device_idx);
	rsc->pipeline = DO(rs2_create_pipeline, rsc->context);
	rsc->config = DO_(rs2_create_config);

	// Set the pipeline to start specifically on the realsense device the prober selected
	bool hdev_has_serial = DO(rs2_supports_device_info, rsc->device, RS2_CAMERA_INFO_SERIAL_NUMBER);
	if (hdev_has_serial) {
		const char *hdev_serial = DO(rs2_get_device_info, rsc->device, RS2_CAMERA_INFO_SERIAL_NUMBER);
		DO(rs2_config_enable_device, rsc->config, hdev_serial);
	} else {
		RS_WARN(rs, "Unexpected, the realsense device in use does not provide a serial number.");
	}

	// Load RealSense pipeline options from json
	rs_source_load_stream_options_from_json(rs);

	// Enable RealSense pipeline streams
	rs2_stream stream_type = rs->stream_type;
	int width = rs->video_width;
	int height = rs->video_height;
	int fps = rs->video_fps;
	rs2_format format = rs->video_format;
	DO(rs2_config_enable_stream, rsc->config, RS2_STREAM_GYRO, 0, 0, 0, RS2_FORMAT_MOTION_XYZ32F, rs->gyro_fps);
	DO(rs2_config_enable_stream, rsc->config, RS2_STREAM_ACCEL, 0, 0, 0, RS2_FORMAT_MOTION_XYZ32F, rs->accel_fps);
	DO(rs2_config_enable_stream, rsc->config, stream_type, rs->stream1_index, width, height, format, fps);
	if (rs->stereo) {
		DO(rs2_config_enable_stream, rsc->config, stream_type, rs->stream2_index, width, height, format, fps);
	}

	// Setup sinks
	rs->left_sink.push_frame = receive_left_frame;
	rs->right_sink.push_frame = receive_right_frame;
	rs->imu_sink.push_imu = receive_imu_sample;
	rs->in_sinks.left = &rs->left_sink;
	rs->in_sinks.right = &rs->right_sink;
	rs->in_sinks.imu = &rs->imu_sink;

	// Setup UI
	u_sink_debug_init(&rs->ui_left_sink);
	u_sink_debug_init(&rs->ui_right_sink);
	m_ff_vec3_f32_alloc(&rs->gyro_ff, 1000);
	m_ff_vec3_f32_alloc(&rs->accel_ff, 1000);
	u_var_add_root(rs, "RealSense Source", false);
	u_var_add_log_level(rs, &rs->ll, "Log Level");
	u_var_add_ro_ff_vec3_f32(rs, rs->gyro_ff, "Gyroscope");
	u_var_add_ro_ff_vec3_f32(rs, rs->accel_ff, "Accelerometer");
	u_var_add_sink_debug(rs, &rs->ui_left_sink, "Left Camera");
	u_var_add_sink_debug(rs, &rs->ui_right_sink, "Right Camera");

	// Setup node
	struct xrt_frame_node *xfn = &rs->node;
	xfn->break_apart = rs_source_node_break_apart;
	xfn->destroy = rs_source_node_destroy;
	xrt_frame_context_add(xfctx, &rs->node);

	// Setup IMU synchronizer lock
	os_mutex_init(&rs->partial_imu_sample.mutex);

	return xfs;
}
