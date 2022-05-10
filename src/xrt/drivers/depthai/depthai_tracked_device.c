// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tiny xrt_device to track your head using a DepthAI device.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_depthai
 */

#include "os/os_time.h"
#include "os/os_threading.h"

#include "util/u_sink.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_tracking.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_frame.h"
#include "util/u_format.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"
#include "math/m_api.h"

#include "tracking/t_tracking.h"

#include "depthai_interface.h"

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include "depthai_interface.h"
#include "util/u_device.h"
#include "math/m_imu_3dof.h"
#include "math/m_relation_history.h"


/*
 *
 * Printing functions.
 *
 */

#define DEPTHAI_TRACE(d, ...) U_LOG_IFL_T(d->log_level, __VA_ARGS__)
#define DEPTHAI_DEBUG(d, ...) U_LOG_IFL_D(d->log_level, __VA_ARGS__)
#define DEPTHAI_INFO(d, ...) U_LOG_IFL_I(d->log_level, __VA_ARGS__)
#define DEPTHAI_WARN(d, ...) U_LOG_IFL_W(d->log_level, __VA_ARGS__)
#define DEPTHAI_ERROR(d, ...) U_LOG_IFL_E(d->log_level, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(depthai_log, "DEPTHAI_LOG", U_LOGGING_INFO)


DEBUG_GET_ONCE_BOOL_OPTION(depthai_3dof, "DEPTHAI_3DOF", false)
DEBUG_GET_ONCE_BOOL_OPTION(depthai_3dof_camera_images, "DEPTHAI_3DOF_CAMERA_IMAGES", false)



struct depthai_xdev
{
	struct xrt_device base;
	struct m_imu_3dof fusion;
	struct xrt_frame_context xfctx;
	struct xrt_frame_sink pretty;
	struct xrt_imu_sink imu_sink;
	struct m_relation_history *rh;
	struct u_sink_debug debug_sink;
	enum u_logging_level log_level;
};

static inline struct depthai_xdev *
depthai_xdev(struct xrt_device *xdev)
{
	return (struct depthai_xdev *)xdev;
}

static void
depthai_3dof_update_inputs(struct xrt_device *xdev)
{
	// Empty
}

static void
depthai_3dof_get_tracked_pose(struct xrt_device *xdev,
                              enum xrt_input_name name,
                              uint64_t at_timestamp_ns,
                              struct xrt_space_relation *out_relation)
{
	struct depthai_xdev *dx = depthai_xdev(xdev);

	if (name != XRT_INPUT_GENERIC_TRACKER_POSE) {
		DEPTHAI_ERROR(dx, "unknown input name");
		return;
	}

	m_relation_history_get(dx->rh, at_timestamp_ns, out_relation);
}

static void
depthai_3dof_get_view_poses(struct xrt_device *xdev,
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
depthai_3dof_destroy(struct xrt_device *xdev)
{
	struct depthai_xdev *dx = depthai_xdev(xdev);

	xrt_frame_context_destroy_nodes(&dx->xfctx);
	m_imu_3dof_close(&dx->fusion);
	m_relation_history_destroy(&dx->rh);
	u_var_remove_root(dx);
	u_device_free(&dx->base);
}

static void
depthai_pretty_push_frame(struct xrt_frame_sink *sink, struct xrt_frame *frame)
{
	struct depthai_xdev *dx = container_of(sink, struct depthai_xdev, pretty);

	u_sink_debug_push_frame(&dx->debug_sink, frame);
}

static void
depthai_receive_imu_sample(struct xrt_imu_sink *imu_sink, struct xrt_imu_sample *imu_sample)
{
	struct depthai_xdev *dx = container_of(imu_sink, struct depthai_xdev, imu_sink);
	DEPTHAI_TRACE(dx, "got IMU sample");

	struct xrt_vec3 a;
	struct xrt_vec3 g;

	a.x = imu_sample->accel_m_s2.x;
	a.y = imu_sample->accel_m_s2.y;
	a.z = imu_sample->accel_m_s2.z;

	g.x = imu_sample->gyro_rad_secs.x;
	g.y = imu_sample->gyro_rad_secs.y;
	g.z = imu_sample->gyro_rad_secs.z;

	m_imu_3dof_update(&dx->fusion, imu_sample->timestamp_ns, &a, &g);

	struct xrt_space_relation rel = {0};
	rel.relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                     XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
	rel.pose.orientation = dx->fusion.rot;

	// m_quat_rotate(rel.pose,)
	struct xrt_quat rot;
	// struct xrt_vec3 x = {0,-1,0};


	struct xrt_vec3 x = {0, -1, 0};
	struct xrt_vec3 z = {0, 0, -1};

	math_quat_from_plus_x_z(&x, &z, &rot);
	math_quat_rotate(&dx->fusion.rot, &rot, &rel.pose.orientation);



	m_relation_history_push(dx->rh, &rel, imu_sample->timestamp_ns);
}

int
depthai_3dof_device_found(struct xrt_prober *xp,
                          struct xrt_prober_device **devices,
                          size_t device_count,
                          size_t index,
                          cJSON *attached_data,
                          struct xrt_device **out_xdev)
{
	bool should_do = debug_get_bool_option_depthai_3dof();

	if (!should_do) {
		return 0;
	}

	bool camera_images = debug_get_bool_option_depthai_3dof_camera_images();

	struct xrt_frame_context xfctx;
	struct xrt_fs *the_fs = NULL;
	if (camera_images) {
		the_fs = depthai_fs_stereo_grayscale_and_imu(&xfctx);
	} else {
		the_fs = depthai_fs_just_imu(&xfctx);
	}
	if (the_fs == NULL) {
		return 0;
	}

	struct depthai_xdev *dx = U_DEVICE_ALLOCATE(struct depthai_xdev, U_DEVICE_ALLOC_TRACKING_NONE, 1, 0);
	dx->log_level = debug_get_log_option_depthai_log();


	dx->xfctx = xfctx;

	m_relation_history_create(&dx->rh);


	dx->base.update_inputs = depthai_3dof_update_inputs;
	dx->base.get_tracked_pose = depthai_3dof_get_tracked_pose;
	dx->base.get_view_poses = depthai_3dof_get_view_poses;
	dx->base.destroy = depthai_3dof_destroy;
	dx->base.name = XRT_DEVICE_DEPTHAI;
	dx->base.tracking_origin->type = XRT_TRACKING_TYPE_OTHER;
	dx->base.tracking_origin->offset = (struct xrt_pose)XRT_POSE_IDENTITY;
	dx->base.inputs[0].name = XRT_INPUT_GENERIC_TRACKER_POSE;
	dx->base.orientation_tracking_supported = true;
	dx->base.position_tracking_supported = true;
	dx->base.device_type = XRT_DEVICE_TYPE_GENERIC_TRACKER;



	// Print name.
	snprintf(dx->base.str, XRT_DEVICE_NAME_LEN, "DepthAI Head Tracker");
	snprintf(dx->base.serial, XRT_DEVICE_NAME_LEN, "DepthAI Head Tracker");



	struct xrt_slam_sinks tmp = {0};

	u_var_add_root(dx, "DepthAI Head Tracker", 0);

	if (camera_images) {
		dx->pretty.push_frame = depthai_pretty_push_frame;
		u_var_add_sink_debug(dx, &dx->debug_sink, "Camera view!");
		u_sink_combiner_create(&dx->xfctx, &dx->pretty, &tmp.left, &tmp.right);
	}

	m_imu_3dof_init(&dx->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_300MS);
	m_imu_3dof_add_vars(&dx->fusion, dx, "");



	dx->imu_sink.push_imu = depthai_receive_imu_sample;
	tmp.imu = &dx->imu_sink;


	xrt_fs_slam_stream_start(the_fs, &tmp);


	out_xdev[0] = &dx->base;
	return 1;
}
