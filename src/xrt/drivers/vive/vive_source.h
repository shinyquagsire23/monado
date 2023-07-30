// Copyright 2022-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface for vive data sources
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include "xrt/xrt_frame.h"
#include "xrt/xrt_tracking.h"

/*!
 * Vive data sources
 *
 * @addtogroup drv_vive
 * @{
 */


#ifdef __cplusplus
extern "C" {
#endif

struct vive_source *
vive_source_create(struct xrt_frame_context *xfctx);

void
vive_source_push_imu_packet(struct vive_source *vs, uint32_t age, timepoint_ns t, struct xrt_vec3 a, struct xrt_vec3 g);

void
vive_source_push_frame_ticks(struct vive_source *vs, timepoint_ns ticks);

void
vive_source_hook_into_sinks(struct vive_source *vs, struct xrt_slam_sinks *sinks);

/*!
 * @}
 */

#ifdef __cplusplus
}
#endif
