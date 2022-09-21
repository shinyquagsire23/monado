// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Tiny xrt_device exposing SLAM capabilities.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_twrap
 */

#include "math/m_vec3.h"

#include "util/u_sink.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_results.h"
#include "xrt/xrt_tracking.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"
#include "math/m_api.h"

#include "tracking/t_tracking.h"

#include <assert.h>

#include "util/u_device.h"
#include "math/m_space.h"
#include "util/u_tracked_imu_3dof.h"


/*
 *
 * Printing functions.
 *
 */

#define SLAM_TRACE(d, ...) U_LOG_IFL_T(d->log_level, __VA_ARGS__)
#define SLAM_DEBUG(d, ...) U_LOG_IFL_D(d->log_level, __VA_ARGS__)
#define SLAM_INFO(d, ...) U_LOG_IFL_I(d->log_level, __VA_ARGS__)
#define SLAM_WARN(d, ...) U_LOG_IFL_W(d->log_level, __VA_ARGS__)
#define SLAM_ERROR(d, ...) U_LOG_IFL_E(d->log_level, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(slam_log, "SLAM_LOG", U_LOGGING_INFO)

struct slam_device
{
	struct xrt_device base;

	enum u_logging_level log_level;

	struct xrt_vec3 pre_rotate_x;
	struct xrt_vec3 pre_rotate_z;

	bool pre_rotate;
	bool use_3dof;
#ifdef XRT_FEATURE_SLAM
	// We do not own this; this gets freed after us when devices on the frame context get freed
	struct xrt_tracked_slam *slam;
	// Ditto
#endif
	struct u_tracked_imu_3dof *dof3;
};

static inline struct slam_device *
slam_device(struct xrt_device *xdev)
{
	return (struct slam_device *)xdev;
}

static void
twrap_slam_update_inputs(struct xrt_device *xdev)
{
	// Empty
}

static void
twrap_slam_get_tracked_pose(struct xrt_device *xdev,
                            enum xrt_input_name name,
                            uint64_t at_timestamp_ns,
                            struct xrt_space_relation *out_relation)
{
	struct slam_device *dx = slam_device(xdev);

	if (name != XRT_INPUT_GENERIC_TRACKER_POSE) {
		SLAM_ERROR(dx, "unknown input name %d", name);
		return;
	}
#ifdef XRT_FEATURE_SLAM
	if (!dx->use_3dof) {

		struct xrt_space_relation basalt_rel = {0};


		xrt_tracked_slam_get_tracked_pose(dx->slam, at_timestamp_ns, &basalt_rel);

		int pose_bits = XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
		bool pose_tracked = basalt_rel.relation_flags & pose_bits;

		if (!pose_tracked) {
			U_ZERO(&out_relation->relation_flags);
			return;
		}

		struct xrt_relation_chain xrc = {0};

		m_relation_chain_push_relation(&xrc, &basalt_rel);



		struct xrt_pose pre = {0};
		math_quat_from_plus_x_z(&dx->pre_rotate_x, &dx->pre_rotate_z, &pre.orientation);
		m_relation_chain_push_pose(&xrc, &pre);

		m_relation_chain_resolve(&xrc, out_relation);
		return;
	}

#endif
	m_relation_history_get(dx->dof3->rh, at_timestamp_ns, out_relation);
}

static void
twrap_slam_get_view_poses(struct xrt_device *xdev,
                          const struct xrt_vec3 *default_eye_relation,
                          uint64_t at_timestamp_ns,
                          uint32_t view_count,
                          struct xrt_space_relation *out_head_relation,
                          struct xrt_fov *out_fovs,
                          struct xrt_pose *out_poses)
{
	assert(false);
}

static void
twrap_slam_destroy(struct xrt_device *xdev)
{
	struct slam_device *dx = slam_device(xdev);


	u_var_remove_root(dx);
	u_device_free(&dx->base);
}

// Does _NOT_ take ownership or free the xfctx
xrt_result_t
twrap_slam_create_device(struct xrt_frame_context *xfctx,
                         enum xrt_device_name name,
                         struct xrt_slam_sinks **out_sinks,
                         struct xrt_device **out_device)
{
	struct slam_device *dx = U_DEVICE_ALLOCATE(struct slam_device, U_DEVICE_ALLOC_TRACKING_NONE, 1, 0);


	dx->log_level = debug_get_log_option_slam_log();



	dx->base.update_inputs = twrap_slam_update_inputs;
	dx->base.get_tracked_pose = twrap_slam_get_tracked_pose;
	dx->base.get_view_poses = twrap_slam_get_view_poses;
	dx->base.destroy = twrap_slam_destroy;
	dx->base.name = name;
	dx->base.tracking_origin->type = XRT_TRACKING_TYPE_OTHER;
	dx->base.tracking_origin->offset = (struct xrt_pose)XRT_POSE_IDENTITY;
	dx->base.inputs[0].name = XRT_INPUT_GENERIC_TRACKER_POSE;
	dx->base.orientation_tracking_supported = true;
	dx->base.position_tracking_supported = true;
	dx->base.device_type = XRT_DEVICE_TYPE_GENERIC_TRACKER;



	// Print name.
	snprintf(dx->base.str, XRT_DEVICE_NAME_LEN, "Generic Inside-Out Head Tracker");
	snprintf(dx->base.serial, XRT_DEVICE_NAME_LEN, "Generic Inside-Out Head Tracker");

#ifdef XRT_FEATURE_SLAM
#ifdef XRT_HAVE_BASALT_SLAM
	// Arrived at mostly by trial and error; seeminly does a 90-degree rotation about the X axis.
	dx->pre_rotate_x = (struct xrt_vec3){1.0f, 0.0f, 0.0f};
	dx->pre_rotate_z = (struct xrt_vec3){0.0f, 1.0f, 0.0f};
	dx->pre_rotate = true;
#else
#pragma message "World space conversion not implemented for this SLAM system"
#endif
#endif

	// note: we can't put this at the very end; we need u_tracked_imu_3dof, and that needs to be put on the debug
	// gui before we link our imu pipeline to it.
	u_var_add_root(dx, "Generic Inside-Out Head Tracker", 0);

	u_var_add_vec3_f32(dx, &dx->pre_rotate_x, "pre_rotate_x");
	u_var_add_vec3_f32(dx, &dx->pre_rotate_z, "pre_rotate_z");
	u_var_add_bool(dx, &dx->pre_rotate, "pre_rotate");
	u_var_add_bool(dx, &dx->use_3dof, "Use 3DOF tracking instead of SLAM");

	// At the end so that it doesn't clutter up the UI
	u_tracked_imu_3dof_create(xfctx, &dx->dof3, dx);

#ifdef XRT_FEATURE_SLAM
	int create_status = t_slam_create(xfctx, NULL, &dx->slam, out_sinks);

	if (create_status != 0 || dx->slam == NULL) {
		twrap_slam_destroy(&dx->base);
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	// Create a split sink at out_sink that pushes to the SLAM IMU sink as well as the 3dof IMU sink, then replace
	// out_sinks's imu sink with the split sink.

	struct xrt_imu_sink *sink_slam = (*out_sinks)->imu;

	struct xrt_imu_sink *tmp = NULL;

	u_imu_sink_split_create(xfctx, &dx->dof3->sink, sink_slam, &tmp);
	u_imu_sink_force_monotonic_create(xfctx, tmp, &tmp);

	(*out_sinks)->imu = tmp;

	int start_status = t_slam_start(dx->slam);

	if (start_status != 0) {
		twrap_slam_destroy(&dx->base);
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}
#endif

	*out_device = &dx->base;
	return XRT_SUCCESS;
}
