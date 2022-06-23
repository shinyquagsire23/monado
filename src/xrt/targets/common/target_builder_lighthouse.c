// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Builder for Lighthouse-tracked devices (vive, index, tundra trackers, etc.)
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#include "util/u_debug.h"
#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_prober.h"

#include "util/u_builders.h"
#include "util/u_config_json.h"
#include "util/u_system_helpers.h"
#include "util/u_device.h"

#include "target_builder_interface.h"

#include "vive/vive_config.h"
#include "xrt/xrt_results.h"

// We compute things using have_{vive,survive} in lighthouse_estimate_system.

#ifdef XRT_BUILD_DRIVER_VIVE
#include "vive/vive_prober.h"
static const bool have_vive = true;
#else
static const bool have_vive = false;
#endif

#ifdef XRT_BUILD_DRIVER_SURVIVE
#include "survive/survive_interface.h"
static const bool have_survive = true;
#else
static const bool have_survive = false;
#endif

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
#include "xrt/xrt_frameserver.h"
#include "util/u_sink.h"
#include "ht/ht_interface.h"
#include "ht_ctrl_emu/ht_ctrl_emu_interface.h"
#include "multi_wrapper/multi.h"
#include "../../tracking/hand/mercury/hg_interface.h"
#endif



#include <assert.h>


DEBUG_GET_ONCE_BOOL_OPTION(vive_over_survive, "SETUP_VIVE_OVER_SURVIVE", false)
DEBUG_GET_ONCE_BOOL_OPTION(ht_use_old_rgb, "HT_USE_OLD_RGB", false)


static bool use_libsurvive = false;

static const char *driver_list[] = {
#ifdef XRT_BUILD_DRIVER_SURVIVE
    "survive",
#endif

#ifdef XRT_BUILD_DRIVER_VIVE
    "vive",
#endif
};

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
struct index_camera_finder
{
	struct xrt_fs *xfs;
	struct xrt_frame_context *xfctx;
	bool found;
};

/*
 *
 * Helper hand-tracking setup functions.
 *
 */

static void
on_video_device(struct xrt_prober *xp,
                struct xrt_prober_device *pdev,
                const char *product,
                const char *manufacturer,
                const char *serial,
                void *ptr)
{
	struct index_camera_finder *finder = (struct index_camera_finder *)ptr;

	// Hardcoded for the Index.
	if (product != NULL && manufacturer != NULL) {
		if ((strcmp(product, "3D Camera") == 0) && (strcmp(manufacturer, "Etron Technology, Inc.") == 0)) {
			xrt_prober_open_video_device(xp, pdev, finder->xfctx, &finder->xfs);
			return;
		}
	}
}


static bool
create_index_optical_hand_tracker(struct u_system_devices *usysd,
                                  struct xrt_prober *xp,
                                  struct t_stereo_camera_calibration *calib,
                                  struct xrt_device **out_hand_tracker)
{
	assert(calib != NULL);


	struct index_camera_finder finder = {0};
	finder.xfctx = &usysd->xfctx;

	xrt_prober_list_video_devices(xp, on_video_device, &finder);


	if (finder.xfs == NULL) {
		return NULL;
	}

	bool old_rgb = debug_get_bool_option_ht_use_old_rgb();



	struct xrt_slam_sinks *sinks = NULL;
	struct xrt_device *hand_tracker_device = NULL;

	struct t_image_boundary_info info;
	info.views[0].type = HT_IMAGE_BOUNDARY_CIRCLE;
	info.views[1].type = HT_IMAGE_BOUNDARY_CIRCLE;


	//!@todo This changes by like 50ish pixels from device to device. For now, the solution is simple: just
	//! make the circle a bit bigger than we'd like.
	// Maybe later we can do vignette calibration? Write a tiny optimizer that tries to fit Index's
	// gradient? Unsure.
	info.views[0].boundary.circle.normalized_center.x = 0.5f;
	info.views[0].boundary.circle.normalized_center.y = 0.5f;

	info.views[1].boundary.circle.normalized_center.x = 0.5f;
	info.views[1].boundary.circle.normalized_center.y = 0.5f;

	info.views[0].boundary.circle.normalized_radius = 0.55;
	info.views[1].boundary.circle.normalized_radius = 0.55;

	ht_device_create(&usysd->xfctx,                                         //
	                 calib,                                                 //
	                 HT_OUTPUT_SPACE_LEFT_CAMERA,                           //
	                 old_rgb ? HT_ALGORITHM_OLD_RGB : HT_ALGORITHM_MERCURY, //
	                 info,                                                  //
	                 &sinks,                                                //
	                 &hand_tracker_device);


	struct xrt_frame_sink *tmp = NULL;

	u_sink_stereo_sbs_to_slam_sbs_create(&usysd->xfctx, sinks->left, sinks->right, &tmp);

	enum xrt_format fmt = old_rgb ? XRT_FORMAT_R8G8B8 : XRT_FORMAT_L8;

	u_sink_create_format_converter(&usysd->xfctx, fmt, tmp, &tmp);

	// This puts the format converter on its own thread, so that nothing gets backed up if it runs slower
	// than the native camera framerate.
	u_sink_simple_queue_create(&usysd->xfctx, tmp, &tmp);

	struct xrt_fs_mode *modes = NULL;
	uint32_t count;

	xrt_fs_enumerate_modes(finder.xfs, &modes, &count);


	bool found_mode = false;
	uint32_t selected_mode = 0;

	for (; selected_mode < count; selected_mode++) {
		if (modes[selected_mode].format == XRT_FORMAT_YUYV422) {
			found_mode = true;
			break;
		}
	}

	if (!found_mode) {
		selected_mode = 0;
	}

	free(modes);

	xrt_fs_stream_start(finder.xfs, tmp, XRT_FS_CAPTURE_TYPE_TRACKING, selected_mode);

	*out_hand_tracker = hand_tracker_device;
	return true;
}

#endif


/*
 *
 * Member functions.
 *
 */

static xrt_result_t
lighthouse_estimate_system(struct xrt_builder *xb,
                           cJSON *config,
                           struct xrt_prober *xp,
                           struct xrt_builder_estimate *estimate)
{

	bool vive_over_survive = debug_get_bool_option_vive_over_survive();
	if (have_survive && have_vive) {
		// We have both drivers - default to libsurvive, but if the user asks specifically for vive we'll give
		// it to them
		use_libsurvive = !vive_over_survive;
	} else if (have_survive) {
		// We only have libsurvive - don't listen to the env var
		if (vive_over_survive) {
			U_LOG_W("Asked for vive, but vive isn't built. Using libsurvive.");
		}
		use_libsurvive = true;
	} else {
		// We only have vive
		use_libsurvive = false;
	}

	U_ZERO(estimate);

	struct u_builder_search_results results = {0};
	struct xrt_prober_device **xpdevs = NULL;
	size_t xpdev_count = 0;
	xrt_result_t xret = XRT_SUCCESS;

	// Lock the device list
	xret = xrt_prober_lock_list(xp, &xpdevs, &xpdev_count);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	bool have_vive = u_builder_find_prober_device(xpdevs, xpdev_count, HTC_VID, VIVE_PID, XRT_BUS_TYPE_USB);
	bool have_vive_pro =
	    u_builder_find_prober_device(xpdevs, xpdev_count, HTC_VID, VIVE_PRO_MAINBOARD_PID, XRT_BUS_TYPE_USB);
	bool have_index =
	    u_builder_find_prober_device(xpdevs, xpdev_count, VALVE_VID, VIVE_PRO_LHR_PID, XRT_BUS_TYPE_USB);


	if (have_vive || have_vive_pro || have_index) {
		estimate->certain.head = true;
		if (use_libsurvive) {
			estimate->maybe.dof6 = true;
			estimate->certain.dof6 = true;
		}
	}

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
	// Valve Indices have UVC stereo cameras on the front. If we've found an Index, we'll probably be able to open
	// the camera and use it to track hands even if we haven't found controllers.
	if (have_index) {
		estimate->maybe.left = true;
		estimate->maybe.right = true;
	}
#endif

	static struct u_builder_search_filter maybe_controller_filters[] = {
	    {VALVE_VID, VIVE_WATCHMAN_DONGLE, XRT_BUS_TYPE_USB},
	    {VALVE_VID, VIVE_WATCHMAN_DONGLE_GEN2, XRT_BUS_TYPE_USB},
	};

	results.xpdev_count = 0;
	xpdev_count = 0;

	u_builder_search(xp, xpdevs, xpdev_count, maybe_controller_filters, ARRAY_SIZE(maybe_controller_filters),
	                 &results);
	if (results.xpdev_count != 0) {
		estimate->maybe.left = true;
		estimate->maybe.right = true;

		// Good assumption that if the user has more than 2 wireless devices, two of them will be controllers
		// and the rest will be vive trackers.
		if (results.xpdev_count > 2) {
			estimate->maybe.extra_device_count = results.xpdev_count - 2;
		}
	}

	estimate->priority = 0;

	xret = xrt_prober_unlock_list(xp, &xpdevs);
	assert(xret == XRT_SUCCESS);

	return XRT_SUCCESS;
}

static xrt_result_t
lighthouse_open_system(struct xrt_builder *xb,
                       cJSON *config,
                       struct xrt_prober *xp,
                       struct xrt_system_devices **out_xsysd)
{
	struct u_system_devices *usysd = u_system_devices_allocate();

	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);


	struct vive_config *hmd_config = NULL;

	if (use_libsurvive) {
#ifdef XRT_BUILD_DRIVER_SURVIVE
		usysd->base.xdev_count += survive_get_devices(&usysd->base.xdevs[usysd->base.xdev_count], &hmd_config);
#endif
	} else {
#ifdef XRT_BUILD_DRIVER_VIVE
		struct xrt_prober_device **xpdevs = NULL;
		size_t xpdev_count = 0;
		xrt_result_t xret = XRT_SUCCESS;

		xret = xrt_prober_lock_list(xp, &xpdevs, &xpdev_count);
		if (xret != XRT_SUCCESS) {
			return xret;
		}
		for (size_t i = 0; i < xpdev_count; i++) {
			struct xrt_prober_device *device = xpdevs[i];
			if (device->bus != XRT_BUS_TYPE_USB) {
				continue;
			}
			if (device->vendor_id != HTC_VID && device->vendor_id != VALVE_VID) {
				continue;
			}
			switch (device->product_id) {
			case VIVE_PID:
			case VIVE_PRO_MAINBOARD_PID:
			case VIVE_PRO_LHR_PID: {
				int num_devices = vive_found(xp, xpdevs, xpdev_count, i, NULL, &hmd_config,
				                             &usysd->base.xdevs[usysd->base.xdev_count]);
				usysd->base.xdev_count += num_devices;

			} break;
			case VIVE_WATCHMAN_DONGLE:
			case VIVE_WATCHMAN_DONGLE_GEN2: {
				int num_devices = vive_controller_found(xp, xpdevs, xpdev_count, i, NULL,
				                                        &usysd->base.xdevs[usysd->base.xdev_count]);
				usysd->base.xdev_count += num_devices;
			} break;
			}
		}
		xrt_prober_unlock_list(xp, &xpdevs);
#endif
	}
	int head_idx = -1;
	int left_idx = -1;
	int right_idx = -1;

	u_device_assign_xdev_roles(usysd->base.xdevs, usysd->base.xdev_count, &head_idx, &left_idx, &right_idx);

	if (head_idx < 0) {
		// We need to at least create a HMD
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}
	usysd->base.roles.head = usysd->base.xdevs[head_idx];

	// It's okay if we didn't find controllers
	if (left_idx >= 0) {
		usysd->base.roles.left = usysd->base.xdevs[left_idx];
		usysd->base.roles.hand_tracking.left =
		    u_system_devices_get_ht_device(usysd, XRT_INPUT_GENERIC_HAND_TRACKING_LEFT);
	}
	if (right_idx >= 0) {
		usysd->base.roles.right = usysd->base.xdevs[right_idx];
		usysd->base.roles.hand_tracking.right =
		    u_system_devices_get_ht_device(usysd, XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT);
	}



#ifdef XRT_BUILD_DRIVER_HANDTRACKING
	// If we have the camera calibration (in hmd_config), and we have a HMD but no controllers, then try to make a
	// hand-tracker device.
	if ((hmd_config != NULL) &&             //
	    (usysd->base.roles.head != NULL) && //
	    (usysd->base.roles.left == NULL) && //
	    (usysd->base.roles.right == NULL)) {
		struct t_stereo_camera_calibration *cal = NULL;

		struct xrt_pose head_in_left_cam;
		if (vive_get_stereo_camera_calibration(hmd_config, &cal, &head_in_left_cam)) {
			struct xrt_device *ht = NULL;
			create_index_optical_hand_tracker(usysd, xp, cal, &ht);
			// It's also okay if we couldn't open the hand tracker
			if (ht != NULL) {
				struct xrt_device *wrap = multi_create_tracking_override(
				    XRT_TRACKING_OVERRIDE_ATTACHED, ht, usysd->base.roles.head,
				    XRT_INPUT_GENERIC_HEAD_POSE, &head_in_left_cam);
				struct xrt_device *two_hands[2];
				cemu_devices_create(usysd->base.roles.head, wrap, two_hands);

				usysd->base.roles.left = two_hands[0];
				usysd->base.roles.right = two_hands[1];
				usysd->base.roles.hand_tracking.left = two_hands[0];
				usysd->base.roles.hand_tracking.right = two_hands[1];
				usysd->base.xdevs[usysd->base.xdev_count++] = two_hands[0];
				usysd->base.xdevs[usysd->base.xdev_count++] = two_hands[1];
			}
			t_stereo_camera_calibration_reference(&cal, NULL);
		}
	}
#endif

	*out_xsysd = &usysd->base;

	return XRT_SUCCESS;
}

static void
lighthouse_destroy(struct xrt_builder *xb)
{
	free(xb);
}


/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_builder *
t_builder_lighthouse_create(void)
{
	struct xrt_builder *xb = U_TYPED_CALLOC(struct xrt_builder);
	xb->estimate_system = lighthouse_estimate_system;
	xb->open_system = lighthouse_open_system;
	xb->destroy = lighthouse_destroy;
	xb->identifier = "lighthouse";
	xb->name = "Lighthouse-tracked (Vive, Index, Tundra trackers, etc.) devices builder";
	xb->driver_identifiers = driver_list;
	xb->driver_identifier_count = ARRAY_SIZE(driver_list);

	return xb;
}
