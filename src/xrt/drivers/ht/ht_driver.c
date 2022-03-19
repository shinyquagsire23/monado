// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking driver code.
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ht
 */

#include "gstreamer/gst_pipeline.h"
#include "gstreamer/gst_sink.h"
#include "ht_interface.h"

#include "../depthai/depthai_interface.h"

#include "util/u_var.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_prober.h"

#include "os/os_time.h"
#include "os/os_threading.h"

#include "math/m_api.h"

#include "util/u_device.h"
#include "util/u_frame.h"
#include "util/u_sink.h"
#include "util/u_format.h"
#include "util/u_logging.h"
#include "util/u_time.h"
#include "util/u_trace_marker.h"
#include "util/u_time.h"
#include "util/u_json.h"
#include "util/u_config_json.h"
#include "util/u_debug.h"


// #include "tracking/t_frame_cv_mat_wrapper.hpp"
// #include "tracking/t_calibration_opencv.hpp"
#include "tracking/t_hand_tracking.h"

// Save me, Obi-Wan!
#include "../../tracking/hand/old_rgb/rgb_interface.h"


#include <cjson/cJSON.h>

DEBUG_GET_ONCE_LOG_OPTION(ht_log, "HT_LOG", U_LOGGING_WARN)


#define HT_TRACE(htd, ...) U_LOG_XDEV_IFL_T(&htd->base, htd->log_level, __VA_ARGS__)
#define HT_DEBUG(htd, ...) U_LOG_XDEV_IFL_D(&htd->base, htd->log_level, __VA_ARGS__)
#define HT_INFO(htd, ...) U_LOG_XDEV_IFL_I(&htd->base, htd->log_level, __VA_ARGS__)
#define HT_WARN(htd, ...) U_LOG_XDEV_IFL_W(&htd->base, htd->log_level, __VA_ARGS__)
#define HT_ERROR(htd, ...) U_LOG_XDEV_IFL_E(&htd->base, htd->log_level, __VA_ARGS__)



struct ht_device
{
	struct xrt_device base;

	struct xrt_tracking_origin tracking_origin; // probably cargo-culted

	enum xrt_format desired_format;

	struct xrt_frame_context xfctx;

	struct xrt_fs *xfs;

	struct xrt_fs_mode mode;

	struct xrt_prober *prober;

	struct t_hand_tracking_sync *sync;
	struct t_hand_tracking_async *async;

	enum u_logging_level log_level;
};

static inline struct ht_device *
ht_device(struct xrt_device *xdev)
{
	return (struct ht_device *)xdev;
}

#if 0
static void
getStartupConfig(struct ht_device *htd, const cJSON *startup_config)
{
	const cJSON *uvc_wire_format = u_json_get(startup_config, "uvc_wire_format");

	if (cJSON_IsString(uvc_wire_format)) {
		bool is_yuv = (strcmp(cJSON_GetStringValue(uvc_wire_format), "yuv") == 0);
		bool is_mjpeg = (strcmp(cJSON_GetStringValue(uvc_wire_format), "mjpeg") == 0);
		if (!is_yuv && !is_mjpeg) {
			HT_WARN(htd, "Unknown wire format type %s - should be \"yuv\" or \"mjpeg\"",
			        cJSON_GetStringValue(uvc_wire_format));
		}
		if (is_yuv) {
			HT_DEBUG(htd, "Using YUYV422!");
			htd->desired_format = XRT_FORMAT_YUYV422;
		} else {
			HT_DEBUG(htd, "Using MJPEG!");
			htd->desired_format = XRT_FORMAT_MJPEG;
		}
	}
}

static void
getUserConfig(struct ht_device *htd)
{
	// The game here is to avoid bugs + be paranoid, not to be fast. If you see something that seems "slow" - don't
	// fix it. Any of the tracking code is way stickier than this could ever be.

	struct u_config_json config_json = {0};

	u_config_json_open_or_create_main_file(&config_json);
	if (!config_json.file_loaded) {
		return;
	}

	cJSON *ht_config_json = cJSON_GetObjectItemCaseSensitive(config_json.root, "config_ht");
	if (ht_config_json == NULL) {
		return;
	}

	// Don't get it twisted: initializing these to NULL is not cargo-culting.
	// Uninitialized values on the stack aren't guaranteed to be 0, so these could end up pointing to what we
	// *think* is a valid address but what is *not* one.
	char *startup_config_string = NULL;

	{
		const cJSON *startup_config_string_json = u_json_get(ht_config_json, "startup_config_index");
		if (cJSON_IsString(startup_config_string_json)) {
			startup_config_string = cJSON_GetStringValue(startup_config_string_json);
		}
	}

	if (startup_config_string != NULL) {
		const cJSON *startup_config_obj =
		    u_json_get(u_json_get(ht_config_json, "startup_configs"), startup_config_string);
		getStartupConfig(htd, startup_config_obj);
	}

	cJSON_Delete(config_json.root);
	return;
}

static void
userConfigSetDefaults(struct ht_device *htd)
{
	htd->desired_format = XRT_FORMAT_YUYV422;
}
#endif

static void
on_video_device(struct xrt_prober *xp,
                struct xrt_prober_device *pdev,
                const char *product,
                const char *manufacturer,
                const char *serial,
                void *ptr)
{
	// Stolen from gui_scene_record

	struct ht_device *htd = (struct ht_device *)ptr;

	// Hardcoded for the Index.
	if (product != NULL && manufacturer != NULL) {
		if ((strcmp(product, "3D Camera") == 0) && (strcmp(manufacturer, "Etron Technology, Inc.") == 0)) {
			xrt_prober_open_video_device(xp, pdev, &htd->xfctx, &htd->xfs);
			return;
		}
	}
}

/*!
 * xrt_device function implementations
 */

static void
ht_device_update_inputs(struct xrt_device *xdev)
{
	// Empty
}

static void
ht_device_get_hand_tracking(struct xrt_device *xdev,
                            enum xrt_input_name name,
                            uint64_t at_timestamp_ns,
                            struct xrt_hand_joint_set *out_value,
                            uint64_t *out_timestamp_ns)
{
	struct ht_device *htd = ht_device(xdev);

	if (name != XRT_INPUT_GENERIC_HAND_TRACKING_LEFT && name != XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
		HT_ERROR(htd, "unknown input name for hand tracker");
		return;
	}

	htd->async->get_hand(htd->async, name, at_timestamp_ns, out_value, out_timestamp_ns);
}

static void
ht_device_destroy(struct xrt_device *xdev)
{
	struct ht_device *htd = ht_device(xdev);
	HT_DEBUG(htd, "called!");


	xrt_frame_context_destroy_nodes(&htd->xfctx);
	// Remove the variable tracking.
	u_var_remove_root(htd);

	u_device_free(&htd->base);
}

struct xrt_device *
ht_device_create(struct xrt_prober *xp, struct t_stereo_camera_calibration *calib)
{
	XRT_TRACE_MARKER();
	assert(calib != NULL);

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_NO_FLAGS;

	//! @todo 2 hands hardcoded
	int num_hands = 2;

	// Allocate device
	struct ht_device *htd = U_DEVICE_ALLOCATE(struct ht_device, flags, num_hands, 0);

	// Setup logging first. We like logging.
	htd->log_level = debug_get_log_option_ht_log();

	// Set defaults - most people won't have a config json and it won't get past here.
	htd->desired_format = XRT_FORMAT_YUYV422;

	htd->prober = xp;
	htd->xfs = NULL;

	xrt_prober_list_video_devices(htd->prober, on_video_device, htd);

	if (htd->xfs == NULL) {
		return NULL;
	}

	htd->base.tracking_origin = &htd->tracking_origin;
	htd->base.tracking_origin->type = XRT_TRACKING_TYPE_RGB;
	htd->base.tracking_origin->offset.position.x = 0.0f;
	htd->base.tracking_origin->offset.position.y = 0.0f;
	htd->base.tracking_origin->offset.position.z = 0.0f;
	htd->base.tracking_origin->offset.orientation.w = 1.0f;


	htd->base.update_inputs = ht_device_update_inputs;
	htd->base.get_hand_tracking = ht_device_get_hand_tracking;
	htd->base.destroy = ht_device_destroy;

	snprintf(htd->base.str, XRT_DEVICE_NAME_LEN, "Camera based Hand Tracker");
	snprintf(htd->base.serial, XRT_DEVICE_NAME_LEN, "Camera based Hand Tracker");

	htd->base.inputs[0].name = XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
	htd->base.inputs[1].name = XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;

	// Yes, you need all of these. Yes, I tried disabling them all one at a time. You need all of these.
	htd->base.name = XRT_DEVICE_HAND_TRACKER;
	htd->base.device_type = XRT_DEVICE_TYPE_HAND_TRACKER;
	htd->base.orientation_tracking_supported = true;
	htd->base.position_tracking_supported = true;
	htd->base.hand_tracking_supported = true;

	htd->sync = t_hand_tracking_sync_old_rgb_create(calib);
	htd->async = t_hand_tracking_async_default_create(&htd->xfctx, htd->sync);

	struct xrt_frame_sink *tmp = NULL;

	u_sink_stereo_sbs_to_slam_sbs_create(&htd->xfctx, &htd->async->left, &htd->async->right, &tmp);

	// Converts images (we'd expect YUV422 or MJPEG) to R8G8B8. Can take a long time, especially on unoptimized
	// builds. If it's really slow, triple-check that you built Monado with optimizations!
	//!@todo
	u_sink_create_format_converter(&htd->xfctx, XRT_FORMAT_R8G8B8, tmp, &tmp);

	// This puts u_sink_create_to_r8g8b8_or_l8 on its own thread, so that nothing gets backed up if it runs slower
	// than the native camera framerate.
	u_sink_simple_queue_create(&htd->xfctx, tmp, &tmp);

	struct xrt_fs_mode *modes;
	uint32_t count;

	xrt_fs_enumerate_modes(htd->xfs, &modes, &count);

	// Index should only have XRT_FORMAT_YUYV422 or XRT_FORMAT_MJPEG.

	bool found_mode = false;
	uint32_t selected_mode = 0;

	for (; selected_mode < count; selected_mode++) {
		if (modes[selected_mode].format == htd->desired_format) {
			found_mode = true;
			break;
		}
	}

	if (!found_mode) {
		selected_mode = 0;
		HT_WARN(htd, "Couldn't find desired camera mode! Something's probably wrong.");
	}

	free(modes);

	xrt_fs_stream_start(htd->xfs, tmp, XRT_FS_CAPTURE_TYPE_TRACKING, selected_mode);

	HT_DEBUG(htd, "Hand Tracker initialized!");

	return &htd->base;
}
