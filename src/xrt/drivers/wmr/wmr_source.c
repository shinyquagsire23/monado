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
#include "math/m_clock_offset.h"
#include "math/m_filter_fifo.h"
#include "util/u_debug.h"
#include "util/u_sink.h"
#include "util/u_var.h"
#include "util/u_trace_marker.h"
#include "xrt/xrt_tracking.h"
#include "xrt/xrt_frameserver.h"

#include <assert.h>
#include <stdio.h>
#include <inttypes.h>

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

	// Sinks (head tracking)
	struct xrt_frame_sink cam_sinks[WMR_MAX_CAMERAS]; //!< Intermediate sinks for camera frames
	struct xrt_imu_sink imu_sink;                     //!< Intermediate sink for IMU samples
	struct xrt_slam_sinks in_sinks;                   //!< Pointers to intermediate sinks
	struct xrt_slam_sinks out_sinks;                  //!< Pointers to downstream sinks

	// UI Sinks (head tracking)
	struct u_sink_debug ui_cam_sinks[WMR_MAX_CAMERAS]; //!< Sink to display camera frames in UI
	struct m_ff_vec3_f32 *gyro_ff;                     //!< Queue of gyroscope data to display in UI
	struct m_ff_vec3_f32 *accel_ff;                    //!< Queue of accelerometer data to display in UI

	bool is_running;              //!< Whether the device is streaming
	bool first_imu_received;      //!< Don't send frames until first IMU sample
	timepoint_ns last_imu_ns;     //!< Last timepoint received.
	time_duration_ns hw2mono;     //!< Estimated offset from IMU to monotonic clock
	time_duration_ns cam_hw2mono; //!< Caches hw2mono for use in the full frame bundle
};

/*
 *
 * Sinks functionality
 *
 */

#define DEFINE_RECEIVE_CAM(cam_id)                                                                                     \
	static void receive_cam##cam_id(struct xrt_frame_sink *sink, struct xrt_frame *xf)                             \
	{                                                                                                              \
		struct wmr_source *ws = container_of(sink, struct wmr_source, cam_sinks[cam_id]);                      \
		if (cam_id == 0) {                                                                                     \
			ws->cam_hw2mono = ws->hw2mono;                                                                 \
		}                                                                                                      \
		xf->timestamp += ws->cam_hw2mono;                                                                      \
		WMR_TRACE(ws, "cam" #cam_id " img t=%" PRId64 " source_t=%" PRId64, xf->timestamp,                     \
		          xf->source_timestamp);                                                                       \
		u_sink_debug_push_frame(&ws->ui_cam_sinks[cam_id], xf);                                                \
		if (ws->out_sinks.cams[cam_id] && ws->first_imu_received) {                                            \
			xrt_sink_push_frame(ws->out_sinks.cams[cam_id], xf);                                           \
		}                                                                                                      \
	}

DEFINE_RECEIVE_CAM(0)
DEFINE_RECEIVE_CAM(1)
DEFINE_RECEIVE_CAM(2)
DEFINE_RECEIVE_CAM(3)

//! Define a function for each WMR_MAX_CAMERAS and reference it in this array
void (*receive_cam[WMR_MAX_CAMERAS])(struct xrt_frame_sink *, struct xrt_frame *) = {
    receive_cam0, //
    receive_cam1, //
    receive_cam2, //
    receive_cam3, //
};

static void
receive_imu_sample(struct xrt_imu_sink *sink, struct xrt_imu_sample *s)
{
	struct wmr_source *ws = container_of(sink, struct wmr_source, imu_sink);

	// Convert hardware timestamp into monotonic clock. Update offset estimate hw2mono.
	// Note this is only done with IMU samples as they have the smallest USB transmission time.
	const float IMU_FREQ = 250.f; //!< @todo use 1000 if "average_imus" is false
	timepoint_ns now_hw = s->timestamp_ns;
	timepoint_ns now_mono = (timepoint_ns)os_monotonic_get_ns();
	timepoint_ns ts = m_clock_offset_a2b(IMU_FREQ, now_hw, now_mono, &ws->hw2mono);

	/*
	 * Check if the timepoint does time travel, we get one or two
	 * old samples when the device has not been cleanly shut down.
	 */
	if (ws->last_imu_ns > ts) {
		WMR_WARN(ws, "Received sample from the past, new: %" PRIu64 ", last: %" PRIu64 ", diff: %" PRIu64, ts,
		         s->timestamp_ns, ts - s->timestamp_ns);
		return;
	}

	ws->first_imu_received = true;
	ws->last_imu_ns = ts;
	s->timestamp_ns = ts;

	struct xrt_vec3_f64 a = s->accel_m_s2;
	struct xrt_vec3_f64 w = s->gyro_rad_secs;
	WMR_TRACE(ws, "imu t=%" PRId64 " a=(%f %f %f) w=(%f %f %f)", ts, a.x, a.y, a.z, w.x, w.y, w.z);

	// Push to debug UI
	struct xrt_vec3 gyro = {(float)w.x, (float)w.y, (float)w.z};
	struct xrt_vec3 accel = {(float)a.x, (float)a.y, (float)a.z};
	m_ff_vec3_f32_push(ws->gyro_ff, &gyro, ts);
	m_ff_vec3_f32_push(ws->accel_ff, &accel, ts);

	if (ws->out_sinks.imu) {
		xrt_sink_push_imu(ws->out_sinks.imu, s);
	}
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
		WMR_INFO(ws, "Starting WMR stream in calibration mode, will stream only cam0 frames");
		ws->out_sinks.cam_count = 1;
		ws->out_sinks.cams[0] = xs;
	} else {
		WMR_ASSERT(false, "Unsupported stream configuration xs=%p capture_type=%d", (void *)xs, capture_type);
		return false;
	}

	bool started = wmr_camera_start(ws->camera);
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
	for (int i = 0; i < ws->config.tcam_count; i++) {
		u_sink_debug_destroy(&ws->ui_cam_sinks[i]);
	}
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
	(void)snprintf(xfs->name, sizeof(xfs->name), WMR_SOURCE_STR);
	(void)snprintf(xfs->product, sizeof(xfs->product), WMR_SOURCE_STR " Product");
	(void)snprintf(xfs->manufacturer, sizeof(xfs->manufacturer), WMR_SOURCE_STR " Manufacturer");
	(void)snprintf(xfs->serial, sizeof(xfs->serial), WMR_SOURCE_STR " Serial");
	xfs->source_id = *((uint64_t *)"WMR_SRC\0");

	// Setup sinks
	for (int i = 0; i < WMR_MAX_CAMERAS; i++) {
		ws->cam_sinks[i].push_frame = receive_cam[i];
	}
	ws->imu_sink.push_imu = receive_imu_sample;

	ws->in_sinks.cam_count = cfg.tcam_count;
	for (int i = 0; i < cfg.tcam_count; i++) {
		ws->in_sinks.cams[i] = &ws->cam_sinks[i];
	}
	ws->in_sinks.imu = &ws->imu_sink;

	struct wmr_camera_open_config options = {
	    .dev_holo = dev_holo,
	    .tcam_confs = cfg.tcams,
	    .tcam_sinks = ws->in_sinks.cams,
	    .tcam_count = cfg.tcam_count,
	    .slam_cam_count = cfg.slam_cam_count,
	    .log_level = ws->log_level,
	};

	ws->camera = wmr_camera_open(&options);
	ws->config = cfg;

	// Setup UI
	for (int i = 0; i < cfg.tcam_count; i++) {
		u_sink_debug_init(&ws->ui_cam_sinks[i]);
	}
	m_ff_vec3_f32_alloc(&ws->gyro_ff, 1000);
	m_ff_vec3_f32_alloc(&ws->accel_ff, 1000);
	u_var_add_root(ws, WMR_SOURCE_STR, false);
	u_var_add_log_level(ws, &ws->log_level, "Log Level");
	u_var_add_ro_ff_vec3_f32(ws, ws->gyro_ff, "Gyroscope");
	u_var_add_ro_ff_vec3_f32(ws, ws->accel_ff, "Accelerometer");
	for (int i = 0; i < cfg.tcam_count; i++) {
		char label[] = "Camera NNNNNNNNNNN";
		(void)snprintf(label, sizeof(label), "Camera %d", i);
		u_var_add_sink_debug(ws, &ws->ui_cam_sinks[i], label);
	}

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
