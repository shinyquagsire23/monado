// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Fake device tracked with EuRoC datasets and SLAM.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup drv_euroc
 */

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_distortion_mesh.h"
#include "util/u_var.h"
#include "math/m_space.h"
#include "math/m_mathinclude.h"
#include "xrt/xrt_prober.h"
#include "xrt/xrt_tracking.h"
#include "xrt/xrt_config_have.h"

#include "euroc_driver.h"

#include <stdio.h>

DEBUG_GET_ONCE_BOOL_OPTION(euroc_hmd, "EUROC_HMD", false)
DEBUG_GET_ONCE_OPTION(euroc_path, "EUROC_PATH", NULL)
DEBUG_GET_ONCE_LOG_OPTION(euroc_log, "EUROC_LOG", U_LOGGING_WARN)

struct xrt_device *
euroc_device_create(struct xrt_prober *xp);

// Euroc Device Prober

/*!
 * @implements xrt_auto_prober
 */
struct euroc_prober
{
	struct xrt_auto_prober base;
};

static inline struct euroc_prober *
euroc_prober(struct xrt_auto_prober *p)
{
	return (struct euroc_prober *)p;
}

static void
euroc_prober_destroy(struct xrt_auto_prober *p)
{
	struct euroc_prober *epr = euroc_prober(p);
	free(epr);
}

static int
euroc_prober_autoprobe(struct xrt_auto_prober *xap,
                       cJSON *attached_data,
                       bool no_hmds,
                       struct xrt_prober *xp,
                       struct xrt_device **out_xdevs)
{
	struct euroc_prober *epr = euroc_prober(xap);
	(void)epr;

	struct xrt_device *xd = euroc_device_create(xp);
	if (xd == NULL) {
		return 0;
	}

	out_xdevs[0] = xd;
	return 1;
}

struct xrt_auto_prober *
euroc_create_auto_prober()
{
	// `ep` var name used for euroc_player, let's use `epr` instead
	struct euroc_prober *epr = U_TYPED_CALLOC(struct euroc_prober);
	epr->base.name = "Euroc Device";
	epr->base.destroy = euroc_prober_destroy;
	epr->base.lelo_dallas_autoprobe = euroc_prober_autoprobe;
	return &epr->base;
}


// Euroc Device

struct euroc_device
{
	struct xrt_device base;
	struct xrt_tracked_slam *slam;
	struct xrt_pose offset;
	struct xrt_pose pose;
	struct xrt_tracking_origin tracking_origin;
	enum u_logging_level log_level;
};

static inline struct euroc_device *
euroc_device(struct xrt_device *xdev)
{
	return (struct euroc_device *)xdev;
}

static void
euroc_device_update_inputs(struct xrt_device *xdev)
{}

//! Corrections specific for original euroc datasets and Kimera.
//! If your datasets comes from a different camera you should probably
//! use a different pose correction function.
XRT_MAYBE_UNUSED static inline struct xrt_pose
euroc_device_correct_pose_from_kimera(struct xrt_pose pose)
{
	//! @todo Implement proper pose corrections for the original euroc datasets
	//! @todo Allow to use different pose corrections depending on the device used to record
	return pose;
}

//! Similar to `euroc_device_correct_pose_from_kimera` but for Basalt.
XRT_MAYBE_UNUSED static inline struct xrt_pose
euroc_device_correct_pose_from_basalt(struct xrt_pose pose)
{
	//! @todo Implement proper pose corrections for the original euroc datasets
	//! @todo Allow to use different pose corrections depending on the device used to record
	return pose;
}

static void
euroc_device_get_tracked_pose(struct xrt_device *xdev,
                              enum xrt_input_name name,
                              uint64_t at_timestamp_ns,
                              struct xrt_space_relation *out_relation)
{
	struct euroc_device *ed = euroc_device(xdev);

	if (ed->slam) {
		EUROC_ASSERT_(at_timestamp_ns < INT64_MAX);
		xrt_tracked_slam_get_tracked_pose(ed->slam, at_timestamp_ns, out_relation);

		int pose_bits = XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
		bool pose_tracked = out_relation->relation_flags & pose_bits;
		if (pose_tracked) {
#if defined(XRT_HAVE_KIMERA_SLAM)
			ed->pose = euroc_device_correct_pose_from_kimera(out_relation->pose);
#elif defined(XRT_HAVE_BASALT_SLAM)
			ed->pose = euroc_device_correct_pose_from_basalt(out_relation->pose);
#else
			ed->pose = out_relation->pose;
#endif
		}
	}

	struct xrt_relation_chain relation_chain = {0};
	m_relation_chain_push_pose(&relation_chain, &ed->pose);
	m_relation_chain_push_pose(&relation_chain, &ed->offset);
	m_relation_chain_resolve(&relation_chain, out_relation);
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
}

static void
euroc_get_view_poses(struct xrt_device *xdev,
                     const struct xrt_vec3 *default_eye_relation,
                     uint64_t at_timestamp_ns,
                     uint32_t view_count,
                     struct xrt_space_relation *out_head_relation,
                     struct xrt_fov *out_fovs,
                     struct xrt_pose *out_poses)
{
	u_device_get_view_poses(xdev, default_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
	                        out_poses);
}

static void
euroc_device_destroy(struct xrt_device *xdev)
{
	struct euroc_device *ed = euroc_device(xdev);
	u_var_remove_root(ed);
	u_device_free(&ed->base);
}

struct xrt_device *
euroc_device_create(struct xrt_prober *xp)
{
	const char *euroc_path = debug_get_option_euroc_path();
	if (euroc_path == NULL) {
		return NULL;
	}

	bool is_hmd = debug_get_bool_option_euroc_hmd();

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_NO_FLAGS;
	if (is_hmd) {
		flags |= U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE;
	}

	struct euroc_device *ed = U_DEVICE_ALLOCATE(struct euroc_device, flags, 1, 0);
	EUROC_ASSERT(ed != NULL, "Unable to allocate device");

	ed->pose = (struct xrt_pose){{0, 0, 0, 1}, {0, 0, 0}};
	ed->offset = (struct xrt_pose){{0, 0, 0, 1}, {0.2, 1.3, -0.5}};
	ed->log_level = debug_get_log_option_euroc_log();

	struct xrt_device *xd = &ed->base;

	const char *dev_name;
	if (is_hmd) {
		xd->name = XRT_DEVICE_GENERIC_HMD;
		xd->device_type = XRT_DEVICE_TYPE_HMD;
		dev_name = "Euroc HMD";
	} else {
		xd->name = XRT_DEVICE_SIMPLE_CONTROLLER;
		xd->device_type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER;
		dev_name = "Euroc Controller";
	}

	snprintf(xd->str, XRT_DEVICE_NAME_LEN, "%s", dev_name);
	snprintf(xd->serial, XRT_DEVICE_NAME_LEN, "%s", dev_name);

	// Fill in xd->hmd
	if (is_hmd) {
		struct u_device_simple_info info;
		info.display.w_pixels = 1280;
		info.display.h_pixels = 720;
		info.display.w_meters = 0.13f;
		info.display.h_meters = 0.07f;
		info.lens_horizontal_separation_meters = 0.13f / 2.0f;
		info.lens_vertical_position_meters = 0.07f / 2.0f;
		info.fov[0] = 85.0f * (M_PI / 180.0f);
		info.fov[1] = 85.0f * (M_PI / 180.0f);

		bool ret = u_device_setup_split_side_by_side(xd, &info);
		EUROC_ASSERT(ret, "Failed to setup HMD properties");

		u_distortion_mesh_set_none(xd);
	}

	xd->tracking_origin = &ed->tracking_origin;
	xd->tracking_origin->type = XRT_TRACKING_TYPE_EXTERNAL_SLAM;
	xd->tracking_origin->offset.orientation.w = 1.0f;
	snprintf(xd->tracking_origin->name, XRT_TRACKING_NAME_LEN, "%s %s", dev_name, "SLAM Tracker");

	if (is_hmd) {
		xd->inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	} else {
		xd->inputs[0].name = XRT_INPUT_SIMPLE_GRIP_POSE;
	}

	xd->update_inputs = euroc_device_update_inputs;
	xd->get_tracked_pose = euroc_device_get_tracked_pose;
	xd->destroy = euroc_device_destroy;
	if (is_hmd) {
		xd->get_view_poses = euroc_get_view_poses;
	}

	u_var_add_root(ed, dev_name, false);
	u_var_add_pose(ed, &ed->pose, "pose");
	u_var_add_pose(ed, &ed->offset, "offset");
	u_var_add_pose(ed, &ed->tracking_origin.offset, "tracking offset");

	bool tracked = xp->tracking->create_tracked_slam(xp->tracking, &ed->slam) >= 0;
	if (!tracked) {
		EUROC_WARN(ed, "Unable to setup the SLAM tracker");
		euroc_device_destroy(xd);
		return NULL;
	}

	return xd;
}
