// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  WMR camera and IMU data source.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup drv_wmr
 */
#include "wmr_source.h"
#include "wmr_camera.h"
#include "wmr_config.h"
#include "wmr_protocol.h"

#include "math/m_api.h"
#include "math/m_filter_fifo.h"
#include "util/u_debug.h"
#include "util/u_sink.h"
#include "util/u_var.h"
#include "util/u_trace_marker.h"
#include "xrt/xrt_tracking.h"
#include "xrt/xrt_frameserver.h"

#include <assert.h>
#include <stdio.h>

#define WMR_SOURCE_STR "WMR Source"

#define WMR_TRACE(w, ...) U_LOG_IFL_T(w->log_level, __VA_ARGS__)
#define WMR_DEBUG(w, ...) U_LOG_IFL_D(w->log_level, __VA_ARGS__)
#define WMR_INFO(w, ...) U_LOG_IFL_I(w->log_level, __VA_ARGS__)
#define WMR_WARN(w, ...) U_LOG_IFL_W(w->log_level, __VA_ARGS__)
#define WMR_ERROR(w, ...) U_LOG_IFL_E(w->log_level, __VA_ARGS__)
#define WMR_ASSERT(predicate, ...)                                                                                     \
	do {                                                                                                           \
		bool p = predicate;                                                                                    \
		if (!p) {                                                                                              \
			U_LOG(U_LOGGING_ERROR, __VA_ARGS__);                                                           \
			assert(false && "WMR_ASSERT failed: " #predicate);                                             \
			exit(EXIT_FAILURE);                                                                            \
		}                                                                                                      \
	} while (false);
#define WMR_ASSERT_(predicate) WMR_ASSERT(predicate, "Assertion failed " #predicate)

DEBUG_GET_ONCE_LOG_OPTION(wmr_log, "WMR_LOG", U_LOGGING_INFO)

/*!
 * Handles all the data sources from the WMR driver
 *
 * @todo Currently only properly handling tracking cameras, move IMU and other sources here
 * @implements xrt_fs
 * @implements xrt_frame_node
 */
struct wmr_source
{
	struct xrt_fs xfs;
	struct xrt_frame_node node;
	enum u_logging_level log_level; //!< Log level

	struct wmr_hmd_config config;
	struct wmr_camera *camera;

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

	bool is_running;              //!< Whether the device is streaming
	bool first_imu_received;      //!< Don't send frames until first IMU sample
	time_duration_ns hw2mono;     //!< Estimated offset from IMU to monotonic clock
	time_duration_ns cam_hw2mono; //!< Cache for hw2mono used in last left frame
};

/*
 *
 * Sinks functionality
 *
 */

/*!
 * Convert a hardware timestamp into monotonic clock. Updates offset estimate.
 * @note Only used with IMU samples as they have the smallest USB transmission time.
 *
 * @param ws wmr_source
 * @param[in, out] ts Hardware timestamp, gets converted to monotonic clock.
 */
static inline void
clock_hw2mono(struct wmr_source *ws, timepoint_ns *ts)
{
	const double alpha = 0.95; // Weight to put on accumulated hw2mono clock offset
	timepoint_ns mono = os_monotonic_get_ns();
	timepoint_ns hw = *ts;
	time_duration_ns old_hw2mono = ws->hw2mono;
	time_duration_ns got_hw2mono = mono - hw;
	time_duration_ns new_hw2mono = old_hw2mono * alpha + got_hw2mono * (1.0 - alpha);
	if (old_hw2mono == 0) { // hw2mono was not set for the first time yet
		new_hw2mono = got_hw2mono;
	}
	ws->hw2mono = new_hw2mono;
	*ts = hw + new_hw2mono;
}

//! Camera specific logic for clock conversion
static inline void
clock_cam_hw2mono(struct wmr_source *ws, struct xrt_frame *xf, bool is_left)
{
	if (is_left) {
		ws->cam_hw2mono = ws->hw2mono; // Cache last hw2mono used for right frame
	}
	xf->timestamp += ws->cam_hw2mono;
}

static void
receive_left_frame(struct xrt_frame_sink *sink, struct xrt_frame *xf)
{
	struct wmr_source *ws = container_of(sink, struct wmr_source, left_sink);
	clock_cam_hw2mono(ws, xf, true);
	WMR_TRACE(ws, "left img t=%ld source_t=%ld", xf->timestamp, xf->source_timestamp);
	u_sink_debug_push_frame(&ws->ui_left_sink, xf);
	if (ws->out_sinks.left && ws->first_imu_received) {
		xrt_sink_push_frame(ws->out_sinks.left, xf);
	}
}

static void
receive_right_frame(struct xrt_frame_sink *sink, struct xrt_frame *xf)
{
	struct wmr_source *ws = container_of(sink, struct wmr_source, right_sink);
	clock_cam_hw2mono(ws, xf, false);
	WMR_TRACE(ws, "right img t=%ld source_t=%ld", xf->timestamp, xf->source_timestamp);
	u_sink_debug_push_frame(&ws->ui_right_sink, xf);
	if (ws->out_sinks.right && ws->first_imu_received) {
		xrt_sink_push_frame(ws->out_sinks.right, xf);
	}
}

static void
receive_imu_sample(struct xrt_imu_sink *sink, struct xrt_imu_sample *s)
{
	struct wmr_source *ws = container_of(sink, struct wmr_source, imu_sink);
	clock_hw2mono(ws, &s->timestamp_ns);
	timepoint_ns ts = s->timestamp_ns;
	struct xrt_vec3_f64 a = s->accel_m_s2;
	struct xrt_vec3_f64 w = s->gyro_rad_secs;
	WMR_TRACE(ws, "imu t=%ld a=(%f %f %f) w=(%f %f %f)", ts, a.x, a.y, a.z, w.x, w.y, w.z);

	// Push to debug UI
	struct xrt_vec3 gyro = {(float)w.x, (float)w.y, (float)w.z};
	struct xrt_vec3 accel = {(float)a.x, (float)a.y, (float)a.z};
	m_ff_vec3_f32_push(ws->gyro_ff, &gyro, ts);
	m_ff_vec3_f32_push(ws->accel_ff, &accel, ts);

	if (ws->out_sinks.imu) {
		xrt_sink_push_imu(ws->out_sinks.imu, s);
	}
	ws->first_imu_received = true;
}


/*
 *
 * Frameserver functionality
 *
 */

static inline struct wmr_source *
wmr_source_from_xfs(struct xrt_fs *xfs)
{
	struct wmr_source *ws = container_of(xfs, struct wmr_source, xfs);
	return ws;
}

static bool
wmr_source_enumerate_modes(struct xrt_fs *xfs, struct xrt_fs_mode **out_modes, uint32_t *out_count)
{
	WMR_ASSERT(false, "Not implemented");
	return false;
}

static bool
wmr_source_configure_capture(struct xrt_fs *xfs, struct xrt_fs_capture_parameters *cp)
{
	WMR_ASSERT(false, "Not implemented");
	return false;
}

static bool
wmr_source_stream_stop(struct xrt_fs *xfs)
{
	DRV_TRACE_MARKER();

	struct wmr_source *ws = wmr_source_from_xfs(xfs);

	bool stopped = wmr_camera_stop(ws->camera);
	if (!stopped) {
		WMR_ERROR(ws, "Unable to stop WMR cameras");
		WMR_ASSERT_(false);
	}

	return stopped;
}

static bool
wmr_source_is_running(struct xrt_fs *xfs)
{
	DRV_TRACE_MARKER();

	struct wmr_source *ws = wmr_source_from_xfs(xfs);
	return ws->is_running;
}

static bool
wmr_source_stream_start(struct xrt_fs *xfs,
                        struct xrt_frame_sink *xs,
                        enum xrt_fs_capture_type capture_type,
                        uint32_t descriptor_index)
{
	DRV_TRACE_MARKER();

	struct wmr_source *ws = wmr_source_from_xfs(xfs);

	if (xs == NULL && capture_type == XRT_FS_CAPTURE_TYPE_TRACKING) {
		WMR_INFO(ws, "Starting WMR stream in tracking mode");
	} else if (xs != NULL && capture_type == XRT_FS_CAPTURE_TYPE_CALIBRATION) {
		WMR_INFO(ws, "Starting WMR stream in calibration mode, will stream only left frames");
		ws->out_sinks.left = xs;
	} else {
		WMR_ASSERT(false, "Unsupported stream configuration xs=%p capture_type=%d", (void *)xs, capture_type);
		return false;
	}

	bool started = wmr_camera_start(ws->camera, ws->config.cameras, ws->config.n_cameras);
	if (!started) {
		WMR_ERROR(ws, "Unable to start WMR cameras");
		WMR_ASSERT_(false);
	}

	ws->is_running = started;
	return ws->is_running;
}

static bool
wmr_source_slam_stream_start(struct xrt_fs *xfs, struct xrt_slam_sinks *sinks)
{
	DRV_TRACE_MARKER();

	struct wmr_source *ws = wmr_source_from_xfs(xfs);
	if (sinks != NULL) {
		ws->out_sinks = *sinks;
	}
	return wmr_source_stream_start(xfs, NULL, XRT_FS_CAPTURE_TYPE_TRACKING, 0);
}


/*
 *
 * Frame node functionality
 *
 */

static void
wmr_source_node_break_apart(struct xrt_frame_node *node)
{
	DRV_TRACE_MARKER();

	struct wmr_source *ws = container_of(node, struct wmr_source, node);
	wmr_source_stream_stop(&ws->xfs);
}

static void
wmr_source_node_destroy(struct xrt_frame_node *node)
{
	DRV_TRACE_MARKER();

	struct wmr_source *ws = container_of(node, struct wmr_source, node);
	WMR_DEBUG(ws, "Destroying WMR source");
	u_sink_debug_destroy(&ws->ui_left_sink);
	u_sink_debug_destroy(&ws->ui_right_sink);
	m_ff_vec3_f32_free(&ws->gyro_ff);
	m_ff_vec3_f32_free(&ws->accel_ff);
	u_var_remove_root(ws);
	if (ws->camera != NULL) { // It could be null if XRT_HAVE_LIBUSB is not defined
		wmr_camera_free(ws->camera);
	}
	free(ws);
}


/*
 *
 * Exported functions
 *
 */

//! Create and open the frame server for IMU/camera streaming.
struct xrt_fs *
wmr_source_create(struct xrt_frame_context *xfctx, struct xrt_prober_device *dev_holo, struct wmr_hmd_config cfg)
{
	DRV_TRACE_MARKER();

	struct wmr_source *ws = U_TYPED_CALLOC(struct wmr_source);
	ws->log_level = debug_get_log_option_wmr_log();

	// Setup xrt_fs
	struct xrt_fs *xfs = &ws->xfs;
	xfs->enumerate_modes = wmr_source_enumerate_modes;
	xfs->configure_capture = wmr_source_configure_capture;
	xfs->stream_start = wmr_source_stream_start;
	xfs->slam_stream_start = wmr_source_slam_stream_start;
	xfs->stream_stop = wmr_source_stream_stop;
	xfs->is_running = wmr_source_is_running;
	snprintf(xfs->name, sizeof(xfs->name), WMR_SOURCE_STR);
	snprintf(xfs->product, sizeof(xfs->product), WMR_SOURCE_STR " Product");
	snprintf(xfs->manufacturer, sizeof(xfs->manufacturer), WMR_SOURCE_STR " Manufacturer");
	snprintf(xfs->serial, sizeof(xfs->serial), WMR_SOURCE_STR " Serial");
	xfs->source_id = 0x574d522d53524300; // WMR_SRC\0 in hex

	// Setup sinks
	ws->left_sink.push_frame = receive_left_frame;
	ws->right_sink.push_frame = receive_right_frame;
	ws->imu_sink.push_imu = receive_imu_sample;
	ws->in_sinks.left = &ws->left_sink;
	ws->in_sinks.right = &ws->right_sink;
	ws->in_sinks.imu = &ws->imu_sink;
	ws->camera = wmr_camera_open(dev_holo, ws->in_sinks.left, ws->in_sinks.right, ws->log_level);
	ws->config = cfg;

	// Setup UI
	u_sink_debug_init(&ws->ui_left_sink);
	u_sink_debug_init(&ws->ui_right_sink);
	m_ff_vec3_f32_alloc(&ws->gyro_ff, 1000);
	m_ff_vec3_f32_alloc(&ws->accel_ff, 1000);
	u_var_add_root(ws, WMR_SOURCE_STR, false);
	u_var_add_log_level(ws, &ws->log_level, "Log Level");
	u_var_add_ro_ff_vec3_f32(ws, ws->gyro_ff, "Gyroscope");
	u_var_add_ro_ff_vec3_f32(ws, ws->accel_ff, "Accelerometer");
	u_var_add_sink_debug(ws, &ws->ui_left_sink, "Left Camera");
	u_var_add_sink_debug(ws, &ws->ui_right_sink, "Right Camera");

	// Setup node
	struct xrt_frame_node *xfn = &ws->node;
	xfn->break_apart = wmr_source_node_break_apart;
	xfn->destroy = wmr_source_node_destroy;
	xrt_frame_context_add(xfctx, &ws->node);

	WMR_DEBUG(ws, "WMR Source created");

	return xfs;
}

void
wmr_source_push_imu_packet(struct xrt_fs *xfs, timepoint_ns t, struct xrt_vec3 accel, struct xrt_vec3 gyro)
{
	DRV_TRACE_MARKER();
	struct wmr_source *ws = wmr_source_from_xfs(xfs);
	struct xrt_vec3_f64 accel_f64 = {accel.x, accel.y, accel.z};
	struct xrt_vec3_f64 gyro_f64 = {gyro.x, gyro.y, gyro.z};
	struct xrt_imu_sample sample = {.timestamp_ns = t, .accel_m_s2 = accel_f64, .gyro_rad_secs = gyro_f64};
	xrt_sink_push_imu(&ws->imu_sink, &sample);
}
