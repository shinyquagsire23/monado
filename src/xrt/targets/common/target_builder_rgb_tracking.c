// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Builder to setup rgb tracking devices into a system.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_drivers.h"

#include "xrt/xrt_prober.h"
#include "xrt/xrt_settings.h"
#include "xrt/xrt_frameserver.h"

#include "util/u_sink.h"
#include "util/u_misc.h"
#include "util/u_device.h"
#include "util/u_logging.h"
#include "util/u_builders.h"
#include "util/u_config_json.h"
#include "util/u_system_helpers.h"

#include "target_builder_interface.h"

#include "simulated/simulated_interface.h"

#ifdef XRT_HAVE_OPENCV
#include "tracking/t_tracking.h"
#endif

#ifdef XRT_BUILD_DRIVER_PSVR
#include "psvr/psvr_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_PSMV
#include "psmv/psmv_interface.h"
#endif

#include <assert.h>


#if !defined(XRT_BUILD_DRIVER_PSVR) && !defined(XRT_BUILD_DRIVER_PSMV)
#error "Must only be built with either XRT_BUILD_DRIVER_PSVR or XRT_BUILD_DRIVER_PSMV set"
#endif


/*
 *
 * Helper functions.
 *
 */

static const char *driver_list[] = {
#ifdef XRT_BUILD_DRIVER_PSVR
    "psvr",
#endif

#ifdef XRT_BUILD_DRIVER_PSMV
    "psmv",
#endif
};

static bool
get_settings(cJSON *json, struct xrt_settings_tracking *settings)
{
	struct u_config_json config_json = {0};
	u_config_json_open_or_create_main_file(&config_json);

	bool bret = u_config_json_get_tracking_settings(&config_json, settings);

	u_config_json_close(&config_json);

	return bret;
}

struct build_state
{
	struct xrt_settings_tracking settings;

	struct xrt_frame_context *xfctx;
	struct xrt_tracking_origin *origin;
	struct xrt_fs *xfs;
	struct xrt_tracked_psvr *psvr;
	struct xrt_tracked_psmv *psmv_red;
	struct xrt_tracked_psmv *psmv_purple;
};

#ifdef XRT_HAVE_OPENCV
static void
on_video_device(struct xrt_prober *xp,
                struct xrt_prober_device *pdev,
                const char *product,
                const char *manufacturer,
                const char *serial,
                void *ptr)
{
	struct build_state *build = (struct build_state *)ptr;

	if (build->xfs != NULL || product == NULL) {
		return;
	}

	if (strcmp(product, build->settings.camera_name) != 0) {
		return;
	}

	xrt_prober_open_video_device(xp, pdev, build->xfctx, &build->xfs);
}

static void
setup_pipeline(struct xrt_prober *xp, struct build_state *build)
{
	struct t_stereo_camera_calibration *data = NULL;

	xrt_prober_list_video_devices(xp, on_video_device, build);

	if (build->xfs == NULL) {
		return;
	}

	// Parse the calibration data from the file.
	if (!t_stereo_camera_calibration_load(build->settings.calibration_path, &data)) {
		return;
	}

	struct xrt_frame_sink *xsink = NULL;
	struct xrt_frame_sink *xsinks[4] = {0};
	struct xrt_colour_rgb_f32 rgb[2] = {{1.f, 0.f, 0.f}, {1.f, 0.f, 1.f}};

	// We create the two psmv trackers up front, but don't start them.
	// clang-format off
	t_psmv_create(build->xfctx, &rgb[0], data, &build->psmv_red, &xsinks[0]);
	t_psmv_create(build->xfctx, &rgb[1], data, &build->psmv_purple, &xsinks[1]);
	t_psvr_create(build->xfctx, data, &build->psvr, &xsinks[2]);
	// clang-format on

	// Setup origin to the common one.
	build->psvr->origin = build->origin;
	build->psmv_red->origin = build->origin;
	build->psmv_purple->origin = build->origin;

	// We create the default multi-channel hsv filter.
	struct t_hsv_filter_params params = T_HSV_DEFAULT_PARAMS();
	t_hsv_filter_create(build->xfctx, &params, xsinks, &xsink);

	// The filter only supports yuv or yuyv formats.
	u_sink_create_to_yuv_or_yuyv(build->xfctx, xsink, &xsink);

	// Put a queue before it to multi-thread the filter.
	u_sink_simple_queue_create(build->xfctx, xsink, &xsink);

	// Hardcoded quirk sink.
	struct u_sink_quirk_params qp;
	U_ZERO(&qp);

	switch (build->settings.camera_type) {
	case XRT_SETTINGS_CAMERA_TYPE_REGULAR_MONO:
		qp.stereo_sbs = false;
		qp.ps4_cam = false;
		qp.leap_motion = false;
		break;
	case XRT_SETTINGS_CAMERA_TYPE_REGULAR_SBS:
		qp.stereo_sbs = true;
		qp.ps4_cam = false;
		qp.leap_motion = false;
		break;
	case XRT_SETTINGS_CAMERA_TYPE_SLAM:
		qp.stereo_sbs = true;
		qp.ps4_cam = false;
		qp.leap_motion = false;
		break;
	case XRT_SETTINGS_CAMERA_TYPE_PS4:
		qp.stereo_sbs = true;
		qp.ps4_cam = true;
		qp.leap_motion = false;
		break;
	case XRT_SETTINGS_CAMERA_TYPE_LEAP_MOTION:
		qp.stereo_sbs = true;
		qp.ps4_cam = false;
		qp.leap_motion = true;
		break;
	}

	u_sink_quirk_create(build->xfctx, xsink, &qp, &xsink);

	// Start the stream now.
	xrt_fs_stream_start(build->xfs, xsink, XRT_FS_CAPTURE_TYPE_TRACKING, build->settings.camera_mode);
}
#endif


/*
 *
 * Member functions.
 *
 */

static xrt_result_t
rgb_estimate_system(struct xrt_builder *xb, cJSON *config, struct xrt_prober *xp, struct xrt_builder_estimate *estimate)
{
	U_ZERO(estimate);

	struct u_builder_search_results results = {0};
	struct xrt_prober_device **xpdevs = NULL;
	size_t xpdev_count = 0;
	xrt_result_t xret = XRT_SUCCESS;
	struct xrt_settings_tracking settings = {0};


	/*
	 * Pre device looking stuff.
	 */

	// Lock the device list
	xret = xrt_prober_lock_list(xp, &xpdevs, &xpdev_count);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	// Is tracking setup?
	if (get_settings(config, &settings)) {
		estimate->certain.dof6 = true;
	}


	/*
	 * Can we find PSVR HND?
	 */

#ifdef XRT_BUILD_DRIVER_PSVR
	struct xrt_prober_device *psvr =
	    u_builder_find_prober_device(xpdevs, xpdev_count, PSVR_VID, PSVR_PID, XRT_BUS_TYPE_USB);
	if (psvr != NULL) {
		estimate->certain.head = true;
	}
#endif


	/*
	 * Can we find any PSMV controllers?
	 */

#ifdef XRT_BUILD_DRIVER_PSMV
	static struct u_builder_search_filter move_filters[2] = {
	    {PSMV_VID, PSMV_PID_ZCM1, XRT_BUS_TYPE_BLUETOOTH},
	    {PSMV_VID, PSMV_PID_ZCM2, XRT_BUS_TYPE_BLUETOOTH},
	};

	u_builder_search(xp, xpdevs, xpdev_count, move_filters, ARRAY_SIZE(move_filters), &results);

	if (results.xpdev_count >= 1) {
		estimate->certain.right = true;
	}

	if (results.xpdev_count >= 2) {
		estimate->certain.left = true;
	}
#endif


	/*
	 * Tidy.
	 */

	xret = xrt_prober_unlock_list(xp, &xpdevs);
	assert(xret == XRT_SUCCESS);

	return XRT_SUCCESS;
}

static xrt_result_t
rgb_open_system(struct xrt_builder *xb, cJSON *config, struct xrt_prober *xp, struct xrt_system_devices **out_xsysd)
{
	struct u_builder_search_results results = {0};
	struct xrt_prober_device **xpdevs = NULL;
	size_t xpdev_count = 0;
	xrt_result_t xret = XRT_SUCCESS;

	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);

	struct u_system_devices *usysd = u_system_devices_allocate();


	/*
	 * Tracking.
	 */

	struct build_state build = {0};
	if (get_settings(config, &build.settings)) {
#ifdef XRT_HAVE_OPENCV
		build.xfctx = &usysd->xfctx;
		build.origin = &usysd->origin;
		build.origin->type = XRT_TRACKING_TYPE_RGB;
		build.origin->offset.orientation.y = 1.0f;
		build.origin->offset.position.z = -2.0f;
		build.origin->offset.position.y = 1.0f;

		setup_pipeline(xp, &build);
#else
		U_LOG_W("Tracking setup but not built with OpenCV/Tracking!");
#endif
	} else {
		U_LOG_I("Not tracking setup in config file, only in 3dof mode available");
	}


	/*
	 * Devices.
	 */

	// Lock the device list
	xret = xrt_prober_lock_list(xp, &xpdevs, &xpdev_count);
	if (xret != XRT_SUCCESS) {
		u_system_devices_destroy(&usysd);
		return xret;
	}

	struct xrt_device *head = NULL;
	struct xrt_device *psmv_red = NULL;
	struct xrt_device *psmv_purple = NULL;

#ifdef XRT_BUILD_DRIVER_PSVR
	struct xrt_prober_device *psvr =
	    u_builder_find_prober_device(xpdevs, xpdev_count, PSVR_VID, PSVR_PID, XRT_BUS_TYPE_USB);
	if (psvr != NULL) {
		head = psvr_device_create(build.psvr);
	}
#endif

	if (head != NULL) {
#ifdef XRT_HAVE_OPENCV
		if (build.psvr != NULL) {
			t_psvr_start(build.psvr);
		}
#endif
	} else {
		head = simulated_hmd_create();
	}


#ifdef XRT_BUILD_DRIVER_PSMV
	static struct u_builder_search_filter move_filters[2] = {
	    {PSMV_VID, PSMV_PID_ZCM1, XRT_BUS_TYPE_BLUETOOTH},
	    {PSMV_VID, PSMV_PID_ZCM2, XRT_BUS_TYPE_BLUETOOTH},
	};

	u_builder_search(xp, xpdevs, xpdev_count, move_filters, ARRAY_SIZE(move_filters), &results);

	if (results.xpdev_count >= 1) {
		psmv_red = psmv_device_create(xp, results.xpdevs[0], build.psmv_red);

#ifdef XRT_HAVE_OPENCV
		if (psmv_red != NULL && build.psmv_red != NULL) {
			t_psmv_start(build.psmv_red);
		}
#endif
	}

	if (results.xpdev_count >= 2) {
		psmv_purple = psmv_device_create(xp, results.xpdevs[1], build.psmv_purple);

#ifdef XRT_HAVE_OPENCV
		if (psmv_purple != NULL && build.psmv_purple != NULL) {
			t_psmv_start(build.psmv_purple);
		}
#endif
	}
#endif

	// Unlock the device list
	xret = xrt_prober_unlock_list(xp, &xpdevs);
	if (xret != XRT_SUCCESS) {
		u_system_devices_destroy(&usysd);
		return xret;
	}


	usysd->base.xdevs[usysd->base.xdev_count++] = head;
	usysd->base.roles.head = head;

	if (psmv_red != NULL) {
		usysd->base.xdevs[usysd->base.xdev_count++] = psmv_red;
		usysd->base.roles.right = psmv_red;
	}

	if (psmv_purple != NULL) {
		usysd->base.xdevs[usysd->base.xdev_count++] = psmv_purple;
		usysd->base.roles.left = psmv_purple;
	}


	/*
	 * Done.
	 */

	*out_xsysd = &usysd->base;

	return XRT_SUCCESS;
}

static void
rgb_destroy(struct xrt_builder *xb)
{
	free(xb);
}


/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_builder *
t_builder_rgb_tracking_create(void)
{
	struct xrt_builder *xb = U_TYPED_CALLOC(struct xrt_builder);
	xb->estimate_system = rgb_estimate_system;
	xb->open_system = rgb_open_system;
	xb->destroy = rgb_destroy;
	xb->identifier = "rgb_tracking";
	xb->name = "RGB tracking based devices (PSVR, PSMV, ...)";
	xb->driver_identifiers = driver_list;
	xb->driver_identifier_count = ARRAY_SIZE(driver_list);

	return xb;
}
