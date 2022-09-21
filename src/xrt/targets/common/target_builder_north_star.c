// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  System builder for North Star headsets
 * @author Nova King <technobaboo@gmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup xrt_iface
 */

#include "math/m_api.h"
#include "math/m_space.h"
#include "multi_wrapper/multi.h"
#include "realsense/rs_interface.h"
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
#include "util/u_file.h"
#include "util/u_pretty_print.h"

#include "target_builder_interface.h"

#include "north_star/ns_interface.h"

#ifdef XRT_BUILD_DRIVER_ULV2
#include "ultraleap_v2/ulv2_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_REALSENSE
#include "realsense/rs_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_DEPTHAI
#include "depthai/depthai_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_TWRAP
#include "twrap/twrap_interface.h"
#endif

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
#include "ht/ht_interface.h"
#endif

#include "ht_ctrl_emu/ht_ctrl_emu_interface.h"

#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_results.h"
#include "xrt/xrt_tracking.h"

#include <assert.h>
#include "math/m_mathinclude.h"

DEBUG_GET_ONCE_OPTION(ns_config_path, "NS_CONFIG_PATH", NULL)
DEBUG_GET_ONCE_LOG_OPTION(ns_log, "NS_LOG", U_LOGGING_WARN)


#define NS_TRACE(...) U_LOG_IFL_T(debug_get_log_option_ns_log(), __VA_ARGS__)
#define NS_DEBUG(...) U_LOG_IFL_D(debug_get_log_option_ns_log(), __VA_ARGS__)
#define NS_INFO(...) U_LOG_IFL_I(debug_get_log_option_ns_log(), __VA_ARGS__)
#define NS_WARN(...) U_LOG_IFL_W(debug_get_log_option_ns_log(), __VA_ARGS__)
#define NS_ERROR(...) U_LOG_IFL_E(debug_get_log_option_ns_log(), __VA_ARGS__)

static const char *driver_list[] = {
    "north_star",
};

struct ns_ultraleap_device
{
	bool active;

	// Users input `P_middleofeyes_to_trackingcenter_oxr`, and we invert it into this pose.
	// It's a lot simpler to (and everybody does) care about the transform from the eyes center to the device,
	// but tracking overrides care about this value.
	struct xrt_pose P_trackingcenter_to_middleofeyes_oxr;
};

struct ns_depthai_device
{
	bool active;
	struct xrt_pose P_imu_to_left_camera_basalt;
	struct xrt_pose P_middleofeyes_to_imu_oxr;
};

struct ns_t265
{
	bool active;
	struct xrt_pose P_middleofeyes_to_trackingcenter_oxr;
};

struct ns_builder
{
	struct xrt_builder base;

	const char *config_path;
	cJSON *config_json;

	struct ns_ultraleap_device ultraleap_device;
	struct ns_depthai_device depthai_device;
	struct ns_t265 t265;
};


static bool
ns_config_load(struct ns_builder *nsb)
{
	const char *file_content = u_file_read_content_from_path(nsb->config_path);
	if (file_content == NULL) {
		U_LOG_E("The file at \"%s\" was unable to load. Either there wasn't a file there or it was empty.",
		        nsb->config_path);
		return false;
	}

	// leaks?
	cJSON *config_json = cJSON_Parse(file_content);

	if (config_json == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		U_LOG_E("The JSON file at path \"%s\" was unable to parse", nsb->config_path);
		if (error_ptr != NULL) {
			U_LOG_E("because of an error before %s", error_ptr);
		}
		free((void *)file_content);
		return false;
	}
	nsb->config_json = config_json;
	free((void *)file_content);
	return true;
}

static void
ns_tracking_config_parse_depthai(struct ns_builder *nsb, bool *out_config_valid)
{
	*out_config_valid = true;
	const cJSON *root = u_json_get(nsb->config_json, "depthaiDevice");

	if (root == NULL) {
		*out_config_valid = true;
		// not invalid, but doesn't exist. active is not set and won't be used
		return;
	}

	*out_config_valid = *out_config_valid && //
	                    u_json_get_bool(u_json_get(root, "active"), &nsb->depthai_device.active);

	*out_config_valid = *out_config_valid && //
	                    u_json_get_pose(u_json_get(root, "P_imu_to_left_camera_basalt"),
	                                    &nsb->depthai_device.P_imu_to_left_camera_basalt);

	*out_config_valid = *out_config_valid && //
	                    u_json_get_pose(u_json_get(root, "P_middleofeyes_to_imu_oxr"),
	                                    &nsb->depthai_device.P_middleofeyes_to_imu_oxr);
}

static void
ns_tracking_config_parse_ultraleap(struct ns_builder *nsb, bool *out_config_valid)
{
	*out_config_valid = true;
	const cJSON *root = u_json_get(nsb->config_json, "leapTracker");
	if (root == NULL) {
		// not invalid, but doesn't exist. active is not set and won't be used
		return;
	}

	struct xrt_pose P_middleofeyes_to_trackingcenter_oxr;

	struct xrt_pose localpose_unity = XRT_POSE_IDENTITY;

	if (u_json_get_pose_permissive(u_json_get(root, "localPose"), &localpose_unity)) {
		NS_INFO(
		    "Found key `localPose` in your Ultraleap tracker config. Converting this from Unity's coordinate "
		    "space to OpenXR's coordinate space.");
		NS_INFO(
		    "If you just want to specify the offset in OpenXR coordinates, use key "
		    "`P_middleofeyes_to_trackingcenter` instead.");


		// This is the conversion from Unity to OpenXR coordinates.
		// Unity: X+ Right; Y+ Up; Z+ Forward
		// OpenXR: X+ Right; Y+ Up; Z- Forward
		// Check tests_quat_change_of_basis to understand the quaternion element negations.
		P_middleofeyes_to_trackingcenter_oxr.position.x = localpose_unity.position.x;
		P_middleofeyes_to_trackingcenter_oxr.position.y = localpose_unity.position.y;
		P_middleofeyes_to_trackingcenter_oxr.position.z = -localpose_unity.position.z;


		P_middleofeyes_to_trackingcenter_oxr.orientation.x = localpose_unity.orientation.x;
		P_middleofeyes_to_trackingcenter_oxr.orientation.y = localpose_unity.orientation.y;
		P_middleofeyes_to_trackingcenter_oxr.orientation.z = -localpose_unity.orientation.z;
		P_middleofeyes_to_trackingcenter_oxr.orientation.w = -localpose_unity.orientation.w;

		*out_config_valid = *out_config_valid && true;
	} else {
		*out_config_valid = *out_config_valid && //
		                    u_json_get_pose(u_json_get(root, "P_middleofeyes_to_trackingcenter_oxr"),
		                                    &P_middleofeyes_to_trackingcenter_oxr);
	}

	math_pose_invert(&P_middleofeyes_to_trackingcenter_oxr,
	                 &nsb->ultraleap_device.P_trackingcenter_to_middleofeyes_oxr);
	nsb->ultraleap_device.active = true;
}

static void
ns_tracking_config_parse_t265(struct ns_builder *nsb, bool *out_config_valid)
{
	*out_config_valid = true;
	const cJSON *root = u_json_get(nsb->config_json, "t265");

	if (root == NULL) {
		// not invalid, but doesn't exist. active is not set and won't be used
		return;
	}

	*out_config_valid = *out_config_valid && //
	                    u_json_get_bool(u_json_get(root, "active"), &nsb->t265.active);

	*out_config_valid = *out_config_valid && //
	                    u_json_get_pose(u_json_get(root, "P_middleofeyes_to_trackingcenter_oxr"),
	                                    &nsb->t265.P_middleofeyes_to_trackingcenter_oxr);
}

void
ns_compute_depthai_ht_offset(struct xrt_pose *P_imu_to_left_camera_basalt, struct xrt_pose *out_pose)
{
	struct xrt_pose deg180 = XRT_POSE_IDENTITY;


	struct xrt_vec3 plusx = {1, 0, 0};
	struct xrt_vec3 plusz = {0, 0, -1};

	math_quat_from_plus_x_z(&plusx, &plusz, &deg180.orientation);

	struct xrt_relation_chain xrc = {0};

	// Remember, relation_chains are backwards.
	// This comes "after" P_imo_to_left_cam_basalt, and rotates from the usual camera coordinate space (+Y down +Z
	// forward) to OpenXR/hand tracking's output coordinate space (+Y up +Z backwards)
	m_relation_chain_push_pose_if_not_identity(&xrc, &deg180);
	// This comes "first" and goes from the head tracking's output space (IMU) to where the left camera is,
	// according to the config file.
	m_relation_chain_push_pose_if_not_identity(&xrc, P_imu_to_left_camera_basalt);

	struct xrt_space_relation rel = {0};

	m_relation_chain_resolve(&xrc, &rel);


	math_pose_invert(&rel.pose, out_pose);
}

#ifdef XRT_BUILD_DRIVER_DEPTHAI
static xrt_result_t
ns_setup_depthai_device(struct ns_builder *nsb,
                        struct u_system_devices *usysd,
                        struct xrt_device **out_hand_device,
                        struct xrt_device **out_head_device)
{
	struct depthai_slam_startup_settings settings = {0};

	settings.frames_per_second = 60;
	settings.half_size_ov9282 = true;
	settings.want_cameras = true;
	settings.want_imu = true;

	struct xrt_fs *the_fs = depthai_fs_slam(&usysd->xfctx, &settings);

	if (the_fs == NULL) {
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	struct t_stereo_camera_calibration *calib = NULL;
	depthai_fs_get_stereo_calibration(the_fs, &calib);



	struct xrt_slam_sinks *hand_sinks = NULL;

	struct t_camera_extra_info extra_camera_info;
	extra_camera_info.views[0].camera_orientation = CAMERA_ORIENTATION_180;
	extra_camera_info.views[1].camera_orientation = CAMERA_ORIENTATION_180;

	extra_camera_info.views[0].boundary_type = HT_IMAGE_BOUNDARY_NONE;
	extra_camera_info.views[1].boundary_type = HT_IMAGE_BOUNDARY_NONE;

	int create_status = ht_device_create(&usysd->xfctx,        //
	                                     calib,                //
	                                     HT_ALGORITHM_MERCURY, //
	                                     extra_camera_info,    //
	                                     &hand_sinks,          //
	                                     out_hand_device);
	t_stereo_camera_calibration_reference(&calib, NULL);
	if (create_status != 0) {
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	struct xrt_slam_sinks *slam_sinks = NULL;
	twrap_slam_create_device(&usysd->xfctx, XRT_DEVICE_DEPTHAI, &slam_sinks, out_head_device);

	struct xrt_slam_sinks entry_sinks = {0};
	struct xrt_frame_sink *entry_left_sink = NULL;
	struct xrt_frame_sink *entry_right_sink = NULL;

	u_sink_split_create(&usysd->xfctx, slam_sinks->left, hand_sinks->left, &entry_left_sink);
	u_sink_split_create(&usysd->xfctx, slam_sinks->right, hand_sinks->right, &entry_right_sink);


	entry_sinks = (struct xrt_slam_sinks){
	    .left = entry_left_sink,
	    .right = entry_right_sink,
	    .imu = slam_sinks->imu,
	    .gt = slam_sinks->gt,
	};

	struct xrt_slam_sinks dummy_slam_sinks = {0};
	dummy_slam_sinks.imu = entry_sinks.imu;

	u_sink_force_genlock_create(&usysd->xfctx, entry_sinks.left, entry_sinks.right, &dummy_slam_sinks.left,
	                            &dummy_slam_sinks.right);

	xrt_fs_slam_stream_start(the_fs, &dummy_slam_sinks);

	return XRT_SUCCESS;
}
#endif

// Note: We're just checking for the config file's existence
static xrt_result_t
ns_estimate_system(struct xrt_builder *xb, cJSON *config, struct xrt_prober *xp, struct xrt_builder_estimate *estimate)
{
	struct ns_builder *nsb = (struct ns_builder *)xb;
	U_ZERO(estimate);

	nsb->config_path = debug_get_option_ns_config_path();

	if (nsb->config_path == NULL) {
		return XRT_SUCCESS;
	}


	struct xrt_prober_device **xpdevs = NULL;
	size_t xpdev_count = 0;
	xrt_result_t xret = XRT_SUCCESS;

	// Lock the device list
	xret = xrt_prober_lock_list(xp, &xpdevs, &xpdev_count);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	estimate->maybe.head = true;
	estimate->certain.head = true;

	bool hand_tracking = false;

#ifdef XRT_BUILD_DRIVER_ULV2
	hand_tracking =
	    hand_tracking || u_builder_find_prober_device(xpdevs, xpdev_count, ULV2_VID, ULV2_PID, XRT_BUS_TYPE_USB);
#endif

#ifdef XRT_BUILD_DRIVER_REALSENSE
	estimate->certain.dof6 =
	    estimate->certain.dof6 || u_builder_find_prober_device(xpdevs, xpdev_count, REALSENSE_MOVIDIUS_VID,
	                                                           REALSENSE_MOVIDIUS_PID, XRT_BUS_TYPE_USB);
	estimate->certain.dof6 =
	    estimate->certain.dof6 || u_builder_find_prober_device(xpdevs, xpdev_count,                  //
	                                                           REALSENSE_TM2_VID, REALSENSE_TM2_PID, //
	                                                           XRT_BUS_TYPE_USB);
#endif

#ifdef XRT_BUILD_DRIVER_DEPTHAI
	bool depthai = u_builder_find_prober_device(xpdevs, xpdev_count, DEPTHAI_VID, DEPTHAI_PID, XRT_BUS_TYPE_USB);
#ifdef XRT_FEATURE_SLAM
	estimate->certain.dof6 = estimate->certain.dof6 || depthai;
#endif
#ifdef XRT_BUILD_DRIVER_HANDTRACKING
	hand_tracking = hand_tracking || depthai;
#endif
#endif

	estimate->certain.left = estimate->certain.right = estimate->maybe.left = estimate->maybe.right = hand_tracking;



	xret = xrt_prober_unlock_list(xp, &xpdevs);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	return XRT_SUCCESS;
}



static xrt_result_t
ns_open_system(struct xrt_builder *xb, cJSON *config, struct xrt_prober *xp, struct xrt_system_devices **out_xsysd)
{
	struct ns_builder *nsb = (struct ns_builder *)xb;


	struct u_system_devices *usysd = u_system_devices_allocate();
	xrt_result_t result = XRT_SUCCESS;

	if (out_xsysd == NULL || *out_xsysd != NULL) {
		NS_ERROR("Invalid output system pointer");
		result = XRT_ERROR_DEVICE_CREATION_FAILED;
		goto end;
	}


	bool load_success = ns_config_load(nsb);
	if (!load_success) {
		result = XRT_ERROR_DEVICE_CREATION_FAILED;
		goto end;
	}

	struct xrt_device *ns_hmd = ns_hmd_create(nsb->config_json);
	if (ns_hmd == NULL) {
		result = XRT_ERROR_DEVICE_CREATION_FAILED;
		goto end;
	}


	bool config_valid = true;
	ns_tracking_config_parse_depthai(nsb, &config_valid);
	if (!config_valid) {
		NS_ERROR("DepthAI device config was invalid!");
	}

	ns_tracking_config_parse_ultraleap(nsb, &config_valid);
	if (!config_valid) {
		NS_ERROR("Leap device config was invalid!");
	}

	ns_tracking_config_parse_t265(nsb, &config_valid);
	if (!config_valid) {
		NS_ERROR("T265 device config was invalid!");
	}

	struct xrt_device *hand_device = NULL;
	struct xrt_device *slam_device = NULL;

	struct xrt_pose head_offset = XRT_POSE_IDENTITY;

	// True if hand tracker is parented to the head tracker (DepthAI), false if hand tracker is parented to
	// middle-of-eyes (Ultraleap etc.)
	bool hand_parented_to_head_tracker = true;
	struct xrt_pose hand_offset = XRT_POSE_IDENTITY;

	// bool got_head_tracker = false;



	// For now we use DepthAI for head tracking + hand tracking OR t265 for head + ultraleap for hand.
	// Mixing systems with more atomicity coming later™️
	if (nsb->depthai_device.active) {
#ifdef XRT_BUILD_DRIVER_DEPTHAI
		NS_INFO("Using DepthAI device!");
		ns_setup_depthai_device(nsb, usysd, &hand_device, &slam_device);
		head_offset = nsb->depthai_device.P_middleofeyes_to_imu_oxr;
		ns_compute_depthai_ht_offset(&nsb->depthai_device.P_imu_to_left_camera_basalt, &hand_offset);
		// got_head_tracker = true;
#else
		NS_ERROR("DepthAI head+hand tracker specified in config but DepthAI support was not compiled in!");
#endif


	} else {
		if (nsb->t265.active) {
#ifdef XRT_BUILD_DRIVER_REALSENSE
			slam_device = rs_create_tracked_device_internal_slam();
			head_offset = nsb->t265.P_middleofeyes_to_trackingcenter_oxr;
			// got_head_tracker = true;
#else
			NS_ERROR(
			    "Realsense head tracker specified in config but Realsense support was not compiled in!");
#endif
		}
		if (nsb->ultraleap_device.active) {
#ifdef XRT_BUILD_DRIVER_ULV2
			ulv2_create_device(&hand_device);
			hand_offset = nsb->ultraleap_device.P_trackingcenter_to_middleofeyes_oxr;
			hand_parented_to_head_tracker = false;
#else
			NS_ERROR(
			    "Ultraleap hand tracker specified in config but Ultraleap support was not compiled in!");
#endif
		}
	}



	struct xrt_device *head_wrap = NULL;

	if (slam_device != NULL) {
		usysd->base.xdevs[usysd->base.xdev_count++] = slam_device;
		head_wrap = multi_create_tracking_override(XRT_TRACKING_OVERRIDE_DIRECT, ns_hmd, slam_device,
		                                           XRT_INPUT_GENERIC_TRACKER_POSE, &head_offset);
	} else {
		// No head tracker, no head tracking.
		head_wrap = ns_hmd;
	}

	usysd->base.xdevs[usysd->base.xdev_count++] = head_wrap;
	usysd->base.roles.head = head_wrap;

	if (hand_device != NULL) {
		// note: hand_parented_to_head_tracker is always false when slam_device is NULL
		struct xrt_device *hand_wrap = multi_create_tracking_override(
		    XRT_TRACKING_OVERRIDE_ATTACHED, hand_device,
		    hand_parented_to_head_tracker ? slam_device : head_wrap,
		    hand_parented_to_head_tracker ? XRT_INPUT_GENERIC_TRACKER_POSE : XRT_INPUT_GENERIC_HEAD_POSE,
		    &hand_offset);
		struct xrt_device *two_hands[2];
		cemu_devices_create(head_wrap, hand_wrap, two_hands);


		// usysd->base.xdev_count = 0;
		usysd->base.xdevs[usysd->base.xdev_count++] = two_hands[0];
		usysd->base.xdevs[usysd->base.xdev_count++] = two_hands[1];


		usysd->base.roles.hand_tracking.left = two_hands[0];
		usysd->base.roles.hand_tracking.right = two_hands[1];

		usysd->base.roles.left = two_hands[0];
		usysd->base.roles.right = two_hands[1];
	}



end:
	if (result == XRT_SUCCESS) {
		*out_xsysd = &usysd->base;
	} else {
		u_system_devices_destroy(&usysd);
	}
	if (nsb->config_json != NULL) {
		cJSON_Delete(nsb->config_json);
	}

	return result;
}

static void
ns_destroy(struct xrt_builder *xb)
{
	free(xb);
}

/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_builder *
t_builder_north_star_create(void)
{
	struct ns_builder *sb = U_TYPED_CALLOC(struct ns_builder);
	sb->base.estimate_system = ns_estimate_system;
	sb->base.open_system = ns_open_system;
	sb->base.destroy = ns_destroy;
	sb->base.identifier = "north_star";
	sb->base.name = "North Star headset";
	sb->base.driver_identifiers = driver_list;
	sb->base.driver_identifier_count = ARRAY_SIZE(driver_list);

	return &sb->base;
}
