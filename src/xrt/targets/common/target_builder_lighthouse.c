// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Builder for Lighthouse-tracked devices (vive, index, tundra trackers, etc.)
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#include "tracking/t_hand_tracking.h"
#include "tracking/t_tracking.h"

#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_prober.h"

#include "util/u_builders.h"
#include "util/u_config_json.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_sink.h"
#include "util/u_system_helpers.h"

#include "target_builder_interface.h"

#include "vive/vive_config.h"
#include "v4l2/v4l2_interface.h"

#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_results.h"
#include "xrt/xrt_tracking.h"

#include <assert.h>

#ifdef XRT_BUILD_DRIVER_VIVE
#include "vive/vive_prober.h"
#include "vive/vive_device.h"
#include "vive/vive_source.h"
#endif

#ifdef XRT_BUILD_DRIVER_SURVIVE
#include "survive/survive_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
#include "ht/ht_interface.h"
#include "ht_ctrl_emu/ht_ctrl_emu_interface.h"
#include "multi_wrapper/multi.h"
#include "../../tracking/hand/mercury/hg_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_OPENGLOVES
#include "opengloves/opengloves_interface.h"
#endif


DEBUG_GET_ONCE_LOG_OPTION(lh_log, "LH_LOG", U_LOGGING_WARN)
DEBUG_GET_ONCE_BOOL_OPTION(vive_over_survive, "VIVE_OVER_SURVIVE", false)
DEBUG_GET_ONCE_BOOL_OPTION(vive_slam, "VIVE_SLAM", true)
DEBUG_GET_ONCE_TRISTATE_OPTION(lh_handtracking, "LH_HANDTRACKING")
DEBUG_GET_ONCE_BOOL_OPTION(ht_use_old_rgb, "HT_USE_OLD_RGB", false)

#define LH_TRACE(...) U_LOG_IFL_T(debug_get_log_option_lh_log(), __VA_ARGS__)
#define LH_DEBUG(...) U_LOG_IFL_D(debug_get_log_option_lh_log(), __VA_ARGS__)
#define LH_INFO(...) U_LOG_IFL_I(debug_get_log_option_lh_log(), __VA_ARGS__)
#define LH_WARN(...) U_LOG_IFL_W(debug_get_log_option_lh_log(), __VA_ARGS__)
#define LH_ERROR(...) U_LOG_IFL_E(debug_get_log_option_lh_log(), __VA_ARGS__)
#define LH_ASSERT(predicate, ...)                                                                                      \
	do {                                                                                                           \
		bool p = predicate;                                                                                    \
		if (!p) {                                                                                              \
			U_LOG(U_LOGGING_ERROR, __VA_ARGS__);                                                           \
			assert(false && "LH_ASSERT failed: " #predicate);                                              \
			exit(EXIT_FAILURE);                                                                            \
		}                                                                                                      \
	} while (false);
#define LH_ASSERT_(predicate) LH_ASSERT(predicate, "Assertion failed " #predicate)

static const char *driver_list[] = {
#ifdef XRT_BUILD_DRIVER_SURVIVE
    "survive",
#endif

#ifdef XRT_BUILD_DRIVER_VIVE
    "vive",
#endif

#ifdef XRT_BUILD_DRIVER_OPENGLOVES
    "opengloves",
#endif
};


struct lighthouse_system
{
	struct xrt_builder base;
	struct u_system_devices *devices;
	bool use_libsurvive; //!< Whether we are using survive driver or vive driver
	bool is_valve_index; //!< Is our HMD a Valve Index? If so, try to set up hand-tracking and SLAM as needed
	struct vive_tracking_status vive_tstatus; //!< Visual tracking status for Index under Vive driver
	struct xrt_fs *xfs;                       //!< Frameserver for Valve Index camera, if we have one.
	struct vive_config *hmd_config;
};


/*
 *
 * Helper tracking setup functions.
 *
 */

static uint32_t
get_selected_mode(struct xrt_fs *xfs)
{
	struct xrt_fs_mode *modes = NULL;
	uint32_t count = 0;
	xrt_fs_enumerate_modes(xfs, &modes, &count);

	LH_ASSERT(count != 0, "No stream modes found in Index camera");

	uint32_t selected_mode = 0;
	for (uint32_t i = 0; i < count; i++) {
		if (modes[i].format == XRT_FORMAT_YUYV422) {
			selected_mode = i;
			break;
		}
	}

	free(modes);
	return selected_mode;
}

static void
on_video_device(struct xrt_prober *xp,
                struct xrt_prober_device *pdev,
                const char *product,
                const char *manufacturer,
                const char *serial,
                void *ptr)
{
	struct lighthouse_system *lhs = (struct lighthouse_system *)ptr;

	// Hardcoded for the Index.
	if (product != NULL && manufacturer != NULL) {
		if ((strcmp(product, "3D Camera") == 0) && (strcmp(manufacturer, "Etron Technology, Inc.") == 0)) {
			xrt_prober_open_video_device(xp, pdev, &lhs->devices->xfctx, &lhs->xfs);
			return;
		}
	}
}

static struct xrt_slam_sinks *
valve_index_slam_track(struct lighthouse_system *lhs)
{
	struct xrt_slam_sinks *sinks = NULL;

#ifdef XRT_FEATURE_SLAM
	struct vive_device *d = (struct vive_device *)lhs->devices->base.roles.head;

	int create_status = t_slam_create(&lhs->devices->xfctx, NULL, &d->tracking.slam, &sinks);
	if (create_status != 0) {
		return NULL;
	}

	int start_status = t_slam_start(d->tracking.slam);
	if (start_status != 0) {
		return NULL;
	}

	LH_INFO("Lighthouse HMD SLAM tracker successfully started");
#endif

	return sinks;
}

static bool
valve_index_hand_track(struct lighthouse_system *lhs,
                       struct xrt_prober *xp,
                       struct xrt_pose head_in_left_cam,
                       struct t_stereo_camera_calibration *stereo_calib,
                       struct xrt_slam_sinks **out_sinks,
                       struct xrt_device **out_devices)
{
#ifdef XRT_BUILD_DRIVER_HANDTRACKING
	struct xrt_device *two_hands[2] = {NULL};
	struct xrt_slam_sinks *sinks = NULL;

	LH_ASSERT_(stereo_calib != NULL);

	// zero-initialized out of paranoia
	struct t_camera_extra_info info = {0};

	info.views[0].camera_orientation = CAMERA_ORIENTATION_0;
	info.views[1].camera_orientation = CAMERA_ORIENTATION_0;

	info.views[0].boundary_type = HT_IMAGE_BOUNDARY_CIRCLE;
	info.views[1].boundary_type = HT_IMAGE_BOUNDARY_CIRCLE;


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

	bool old_rgb = debug_get_bool_option_ht_use_old_rgb();
	enum t_hand_tracking_algorithm ht_algorithm = old_rgb ? HT_ALGORITHM_OLD_RGB : HT_ALGORITHM_MERCURY;

	struct xrt_device *ht_device = NULL;
	int create_status = ht_device_create(&lhs->devices->xfctx, //
	                                     stereo_calib,         //
	                                     ht_algorithm,         //
	                                     info,                 //
	                                     &sinks,               //
	                                     &ht_device);
	if (create_status != 0) {
		LH_WARN("Failed to create hand tracking device\n");
		return false;
	}

	ht_device =
	    multi_create_tracking_override(XRT_TRACKING_OVERRIDE_ATTACHED, ht_device, lhs->devices->base.roles.head,
	                                   XRT_INPUT_GENERIC_HEAD_POSE, &head_in_left_cam);

	int created_devices = cemu_devices_create(lhs->devices->base.roles.head, ht_device, two_hands);
	if (created_devices != 2) {
		LH_WARN("Unexpected amount of hand devices created (%d)\n", create_status);
		xrt_device_destroy(&ht_device);
		return false;
	}

	LH_INFO("Hand tracker successfully created\n");

	*out_sinks = sinks;
	out_devices[0] = two_hands[0];
	out_devices[1] = two_hands[1];
	return true;
#endif

	return false;
}

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
	struct lighthouse_system *lhs = (struct lighthouse_system *)xb;
#ifdef XRT_BUILD_DRIVER_VIVE
	bool have_vive_drv = true;
#else
	bool have_vive_drv = false;
#endif

#ifdef XRT_BUILD_DRIVER_SURVIVE
	bool have_survive_drv = true;
#else
	bool have_survive_drv = false;
#endif

	bool vive_over_survive = debug_get_bool_option_vive_over_survive();
	if (have_survive_drv && have_vive_drv) {
		// We have both drivers - default to libsurvive, but if the user asks specifically for vive we'll give
		// it to them
		lhs->use_libsurvive = !vive_over_survive;
	} else if (have_survive_drv) {
		// We only have libsurvive - don't listen to the env var
		// Note: this is a super edge-case, Vive gets built by default on Linux.
		if (vive_over_survive) {
			LH_WARN("Asked for vive driver, but it isn't built. Using libsurvive.");
		}
		lhs->use_libsurvive = true;
	} else if (have_vive_drv) {
		// We only have vive
		lhs->use_libsurvive = false;
	} else {
		LH_ASSERT_(false);
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
	lhs->is_valve_index =
	    u_builder_find_prober_device(xpdevs, xpdev_count, VALVE_VID, VIVE_PRO_LHR_PID, XRT_BUS_TYPE_USB);


	if (have_vive || have_vive_pro || lhs->is_valve_index) {
		estimate->certain.head = true;
		if (lhs->use_libsurvive) {
			estimate->maybe.dof6 = true;
			estimate->certain.dof6 = true;
		}
	}

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
	// Valve Indices have UVC stereo cameras on the front. If we've found an Index, we'll probably be able to open
	// the camera and use it to track hands even if we haven't found controllers.
	if (lhs->is_valve_index) {
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
	LH_ASSERT_(xret == XRT_SUCCESS);

	return XRT_SUCCESS;
}

// If the HMD is a Valve Index, decide if we want visual (HT/Slam) trackers, and if so set them up.
static bool
valve_index_setup_visual_trackers(struct lighthouse_system *lhs,
                                  struct xrt_prober *xp,
                                  struct xrt_slam_sinks *out_sinks,
                                  struct xrt_device **out_devices)
{
	bool slam_enabled = lhs->vive_tstatus.slam_enabled;
	bool hand_enabled = lhs->vive_tstatus.hand_enabled;

	struct t_stereo_camera_calibration *stereo_calib = NULL;
	struct xrt_pose head_in_left_cam;
	vive_get_stereo_camera_calibration(lhs->hmd_config, &stereo_calib, &head_in_left_cam);

	// Initialize SLAM tracker
	struct xrt_slam_sinks *slam_sinks = NULL;
	if (slam_enabled) {
		slam_sinks = valve_index_slam_track(lhs);
		if (slam_sinks == NULL) {
			lhs->vive_tstatus.slam_enabled = false;
			slam_enabled = false;
			LH_WARN("Unable to setup the SLAM tracker");
		}
	}

	// Initialize hand tracker
	struct xrt_slam_sinks *hand_sinks = NULL;
	struct xrt_device *hand_devices[2] = {NULL};
	if (hand_enabled) {
		bool success =
		    valve_index_hand_track(lhs, xp, head_in_left_cam, stereo_calib, &hand_sinks, hand_devices);
		if (!success) {
			lhs->vive_tstatus.hand_enabled = false;
			hand_enabled = false;
			LH_WARN("Unable to setup the hand tracker");
		}
	}

	t_stereo_camera_calibration_reference(&stereo_calib, NULL);

	if (!lhs->use_libsurvive) { // Refresh trackers status in vive driver
		struct vive_device *d = (struct vive_device *)lhs->devices->base.roles.head;
		vive_set_trackers_status(d, lhs->vive_tstatus);
	}

	// Setup frame graph

	struct xrt_frame_sink *entry_left_sink = NULL;
	struct xrt_frame_sink *entry_right_sink = NULL;
	struct xrt_frame_sink *entry_sbs_sink = NULL;
	bool old_rgb_ht = debug_get_bool_option_ht_use_old_rgb();

	if (slam_enabled && hand_enabled && !old_rgb_ht) {
		u_sink_split_create(&lhs->devices->xfctx, slam_sinks->left, hand_sinks->left, &entry_left_sink);
		u_sink_split_create(&lhs->devices->xfctx, slam_sinks->right, hand_sinks->right, &entry_right_sink);
		u_sink_stereo_sbs_to_slam_sbs_create(&lhs->devices->xfctx, entry_left_sink, entry_right_sink,
		                                     &entry_sbs_sink);
		u_sink_create_format_converter(&lhs->devices->xfctx, XRT_FORMAT_L8, entry_sbs_sink, &entry_sbs_sink);
	} else if (slam_enabled && hand_enabled && old_rgb_ht) {
		struct xrt_frame_sink *hand_sbs = NULL;
		struct xrt_frame_sink *slam_sbs = NULL;
		u_sink_stereo_sbs_to_slam_sbs_create(&lhs->devices->xfctx, hand_sinks->left, hand_sinks->right,
		                                     &hand_sbs);
		u_sink_stereo_sbs_to_slam_sbs_create(&lhs->devices->xfctx, slam_sinks->left, slam_sinks->right,
		                                     &slam_sbs);
		u_sink_create_format_converter(&lhs->devices->xfctx, XRT_FORMAT_L8, slam_sbs, &slam_sbs);
		u_sink_split_create(&lhs->devices->xfctx, slam_sbs, hand_sbs, &entry_sbs_sink);
	} else if (slam_enabled) {
		entry_left_sink = slam_sinks->left;
		entry_right_sink = slam_sinks->right;
		u_sink_stereo_sbs_to_slam_sbs_create(&lhs->devices->xfctx, entry_left_sink, entry_right_sink,
		                                     &entry_sbs_sink);
		u_sink_create_format_converter(&lhs->devices->xfctx, XRT_FORMAT_L8, entry_sbs_sink, &entry_sbs_sink);
	} else if (hand_enabled) {
		enum xrt_format fmt = old_rgb_ht ? XRT_FORMAT_R8G8B8 : XRT_FORMAT_L8;
		entry_left_sink = hand_sinks->left;
		entry_right_sink = hand_sinks->right;
		u_sink_stereo_sbs_to_slam_sbs_create(&lhs->devices->xfctx, entry_left_sink, entry_right_sink,
		                                     &entry_sbs_sink);
		u_sink_create_format_converter(&lhs->devices->xfctx, fmt, entry_sbs_sink, &entry_sbs_sink);
	} else {
		LH_WARN("No visual trackers were set");
		return false;
	}
	//! @todo Using a single slot queue is wrong for SLAM
	u_sink_simple_queue_create(&lhs->devices->xfctx, entry_sbs_sink, &entry_sbs_sink);

	struct xrt_slam_sinks entry_sinks = {
	    .left = entry_sbs_sink,
	    .right = NULL, // v4l2 streams a single SBS frame so we ignore the right sink
	    .imu = slam_enabled ? slam_sinks->imu : NULL,
	    .gt = slam_enabled ? slam_sinks->gt : NULL,
	};

	*out_sinks = entry_sinks;
	if (hand_enabled) {
		out_devices[0] = hand_devices[0];
		out_devices[1] = hand_devices[1];
	}
	return true;
}



static bool
stream_data_sources(struct lighthouse_system *lhs, struct xrt_prober *xp, struct xrt_slam_sinks sinks)
{
	// Open frame server
	xrt_prober_list_video_devices(xp, on_video_device, lhs);
	if (lhs->xfs == NULL) {
		LH_WARN("Couldn't find Index camera at all. Is it plugged in?");
		xrt_frame_context_destroy_nodes(&lhs->devices->xfctx);
		return false;
	}

	bool success = false;
	uint32_t mode = get_selected_mode(lhs->xfs);

	// If SLAM is enabled (only on vive driver) we intercept the data sink
	if (lhs->vive_tstatus.slam_enabled) {
		struct vive_device *d = (struct vive_device *)lhs->devices->base.roles.head;
		LH_ASSERT_(d != NULL && d->source != NULL);
		struct vive_source *vs = d->source;
		vive_source_hook_into_sinks(vs, &sinks);
	}

	success = xrt_fs_stream_start(lhs->xfs, sinks.left, XRT_FS_CAPTURE_TYPE_TRACKING, mode);

	if (!success) {
		LH_ERROR("Unable to start data streaming");
		xrt_frame_context_destroy_nodes(&lhs->devices->xfctx);
	}

	return success;
}

static void
try_add_opengloves(struct u_system_devices *usysd)
{
#ifdef XRT_BUILD_DRIVER_OPENGLOVES
	size_t openglove_device_count =
	    opengloves_create_devices(&usysd->base.xdevs[usysd->base.xdev_count], &usysd->base);
	for (size_t i = usysd->base.xdev_count; i < usysd->base.xdev_count + openglove_device_count; i++) {
		struct xrt_device *xdev = usysd->base.xdevs[i];

		for (uint32_t j = 0; j < xdev->input_count; j++) {
			struct xrt_input *input = &xdev->inputs[j];

			if (input->name == XRT_INPUT_GENERIC_HAND_TRACKING_LEFT) {
				usysd->base.roles.hand_tracking.left = xdev;

				break;
			}
			if (input->name == XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
				usysd->base.roles.hand_tracking.right = xdev;

				break;
			}
		}
	}

	usysd->base.xdev_count += openglove_device_count;

#endif
}

static xrt_result_t
lighthouse_open_system(struct xrt_builder *xb,
                       cJSON *config,
                       struct xrt_prober *xp,
                       struct xrt_system_devices **out_xsysd)
{
	struct lighthouse_system *lhs = (struct lighthouse_system *)xb;
	lhs->devices = u_system_devices_allocate();
	struct u_system_devices *usysd = lhs->devices;

	xrt_result_t result = XRT_SUCCESS;

	if (out_xsysd == NULL || *out_xsysd != NULL) {
		LH_ERROR("Invalid output system pointer");
		result = XRT_ERROR_DEVICE_CREATION_FAILED;
		goto end;
	}

	// Decide whether to initialize the SLAM tracker
	bool slam_wanted = debug_get_bool_option_vive_slam();
#ifdef XRT_FEATURE_SLAM
	bool slam_supported = !lhs->use_libsurvive; // Only with vive driver
#else
	bool slam_supported = false;
#endif
	bool slam_enabled = slam_supported && slam_wanted;

	// Decide whether to initialize the hand tracker
#ifdef XRT_BUILD_DRIVER_HANDTRACKING
	bool hand_supported = true;
#else
	bool hand_supported = false;
#endif

	struct vive_tracking_status tstatus = {.slam_wanted = slam_wanted,
	                                       .slam_supported = slam_supported,
	                                       .slam_enabled = slam_enabled,
	                                       .controllers_found = false,
	                                       .hand_supported = hand_supported,
	                                       .hand_wanted = debug_get_tristate_option_lh_handtracking()};
	lhs->vive_tstatus = tstatus;

	if (lhs->use_libsurvive) {
#ifdef XRT_BUILD_DRIVER_SURVIVE
		usysd->base.xdev_count +=
		    survive_get_devices(&usysd->base.xdevs[usysd->base.xdev_count], &lhs->hmd_config);
#endif
	} else {
#ifdef XRT_BUILD_DRIVER_VIVE
		struct xrt_prober_device **xpdevs = NULL;
		size_t xpdev_count = 0;

		result = xrt_prober_lock_list(xp, &xpdevs, &xpdev_count);
		if (result != XRT_SUCCESS) {
			LH_ERROR("Unable to lock the prober dev list");
			goto end;
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
				struct vive_source *vs = vive_source_create(&usysd->xfctx);
				int num_devices =
				    vive_found(xp, xpdevs, xpdev_count, i, NULL, lhs->vive_tstatus, vs,
				               &lhs->hmd_config, &usysd->base.xdevs[usysd->base.xdev_count]);
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
		LH_ERROR("Unable to find HMD");
		result = XRT_ERROR_DEVICE_CREATION_FAILED;
		goto end;
	}
	usysd->base.roles.head = usysd->base.xdevs[head_idx];

	// It's okay if we didn't find controllers
	if (left_idx >= 0) {
		lhs->vive_tstatus.controllers_found = true;
		usysd->base.roles.left = usysd->base.xdevs[left_idx];
		usysd->base.roles.hand_tracking.left =
		    u_system_devices_get_ht_device(usysd, XRT_INPUT_GENERIC_HAND_TRACKING_LEFT);
	}

	if (right_idx >= 0) {
		lhs->vive_tstatus.controllers_found = true;
		usysd->base.roles.right = usysd->base.xdevs[right_idx];
		usysd->base.roles.hand_tracking.right =
		    u_system_devices_get_ht_device(usysd, XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT);
	}

	if (lhs->is_valve_index) {
		if (lhs->vive_tstatus.hand_wanted == DEBUG_TRISTATE_ON) {
			lhs->vive_tstatus.hand_enabled = true;
		} else if (lhs->vive_tstatus.hand_wanted == DEBUG_TRISTATE_AUTO) {
			if (lhs->vive_tstatus.controllers_found) {
				lhs->vive_tstatus.hand_enabled = false;
			} else {
				lhs->vive_tstatus.hand_enabled = true;
			}
		} else if (lhs->vive_tstatus.hand_wanted == DEBUG_TRISTATE_OFF) {
			lhs->vive_tstatus.hand_enabled = false;
		}



		bool success = true;

		if (lhs->hmd_config == NULL) {
			// This should NEVER happen, but we're not writing Rust.
			U_LOG_E("Didn't get a vive config? Not creating visual trackers.");
			goto end;
		}
		if (!lhs->hmd_config->cameras.valid) {
			U_LOG_I(
			    "HMD didn't have cameras or didn't have a valid camera calibration. Not creating visual "
			    "trackers.");
			goto end;
		}

		struct xrt_slam_sinks sinks = {0};
		struct xrt_device *hand_devices[2] = {NULL};
		success = valve_index_setup_visual_trackers(lhs, xp, &sinks, hand_devices);
		if (!success) {
			result = XRT_SUCCESS; // We won't have trackers, but creation was otherwise ok
			goto end;
		}

		if (lhs->vive_tstatus.hand_enabled) {
			if (hand_devices[0] != NULL) {
				usysd->base.roles.left = hand_devices[0];
				usysd->base.roles.hand_tracking.left = hand_devices[0];
				usysd->base.xdevs[usysd->base.xdev_count++] = hand_devices[0];
			}

			if (hand_devices[1] != NULL) {
				usysd->base.roles.right = hand_devices[1];
				usysd->base.roles.hand_tracking.right = hand_devices[1];
				usysd->base.xdevs[usysd->base.xdev_count++] = hand_devices[1];
			}
		}

		success = stream_data_sources(lhs, xp, sinks);
		if (!success) {
			result = XRT_SUCCESS; // We can continue after freeing trackers
			goto end;
		}
	}



end:
	if (!lhs->vive_tstatus.hand_enabled) {
		// We only want to try to add opengloves if we aren't optically tracking hands
		try_add_opengloves(usysd);
	}

	if (result == XRT_SUCCESS) {
		*out_xsysd = &usysd->base;
	} else {
		u_system_devices_destroy(&usysd);
	}

	return result;
}

static void
lighthouse_destroy(struct xrt_builder *xb)
{
	struct lighthouse_system *lhs = (struct lighthouse_system *)xb;
	free(lhs);
}


/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_builder *
t_builder_lighthouse_create(void)
{
	struct lighthouse_system *lhs = U_TYPED_CALLOC(struct lighthouse_system);
	lhs->base.estimate_system = lighthouse_estimate_system;
	lhs->base.open_system = lighthouse_open_system;
	lhs->base.destroy = lighthouse_destroy;
	lhs->base.identifier = "lighthouse";
	lhs->base.name = "Lighthouse-tracked (Vive, Index, Tundra trackers, etc.) devices builder";
	lhs->base.driver_identifiers = driver_list;
	lhs->base.driver_identifier_count = ARRAY_SIZE(driver_list);

	return &lhs->base;
}
