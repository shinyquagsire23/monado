// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Wrapper for m_imu_3dof that can be placed inside (and freed along with!) an `xrt_imu_sink` pipeline.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "math/m_imu_3dof.h"
#include "math/m_relation_history.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_tracking.h"


#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @see u_tracked_imu_3dof_create
 */
struct u_tracked_imu_3dof
{
	struct xrt_imu_sink sink;
	struct xrt_frame_node node;

	struct m_imu_3dof fusion;
	struct m_relation_history *rh;
};


/*!
 * @see xrt_frame_context
 * Creates a wrapper for m_imu_3dof that can be placed inside (and freed along with!) an `xrt_imu_sink` pipeline.
 * Useful when your frameserver is significantly separated from your xrt_device
 */
void
u_tracked_imu_3dof_create(struct xrt_frame_context *xfctx, struct u_tracked_imu_3dof **out_3dof, void *debug_var_root);


#ifdef __cplusplus
}
#endif
