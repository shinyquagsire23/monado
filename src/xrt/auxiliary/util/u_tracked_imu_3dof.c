// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Wrapper for m_imu_3dof that can be placed inside (and freed along with!) an `xrt_imu_sink` pipeline.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_misc.h"
#include "util/u_tracked_imu_3dof.h"


static void
u_tracked_imu_receive_imu_sample(struct xrt_imu_sink *imu_sink, struct xrt_imu_sample *imu_sample)
{
	struct u_tracked_imu_3dof *dof3 = container_of(imu_sink, struct u_tracked_imu_3dof, sink);

	struct xrt_vec3 a;
	struct xrt_vec3 g;

	a.x = imu_sample->accel_m_s2.x;
	a.y = imu_sample->accel_m_s2.y;
	a.z = imu_sample->accel_m_s2.z;

	g.x = imu_sample->gyro_rad_secs.x;
	g.y = imu_sample->gyro_rad_secs.y;
	g.z = imu_sample->gyro_rad_secs.z;

	m_imu_3dof_update(&dof3->fusion, imu_sample->timestamp_ns, &a, &g);

	struct xrt_space_relation rel = {0};
	rel.relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                     XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
	rel.pose.orientation = dof3->fusion.rot;

	m_relation_history_push(dof3->rh, &rel, imu_sample->timestamp_ns);
}

static void
u_tracked_imu_node_break_apart(struct xrt_frame_node *imu_node)
{}

static void
u_tracked_imu_node_destroy(struct xrt_frame_node *imu_node)
{
	struct u_tracked_imu_3dof *dof3 = container_of(imu_node, struct u_tracked_imu_3dof, node);

	m_imu_3dof_close(&dof3->fusion);
	m_relation_history_destroy(&dof3->rh);

	free(dof3);
}

void
u_tracked_imu_3dof_create(struct xrt_frame_context *xfctx, struct u_tracked_imu_3dof **out_3dof, void *debug_var_root)
{
	struct u_tracked_imu_3dof *dof3 = U_TYPED_CALLOC(struct u_tracked_imu_3dof);

	m_relation_history_create(&dof3->rh);

	m_imu_3dof_init(&dof3->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_300MS);
	m_imu_3dof_add_vars(&dof3->fusion, debug_var_root, "");

	dof3->sink.push_imu = u_tracked_imu_receive_imu_sample;

	dof3->node.break_apart = u_tracked_imu_node_break_apart;
	dof3->node.destroy = u_tracked_imu_node_destroy;

	xrt_frame_context_add(xfctx, &dof3->node);

	*out_3dof = dof3;
}
