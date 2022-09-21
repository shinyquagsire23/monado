// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Tiny xrt_device exposing SLAM capabilities.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_twrap
 */

#pragma once

#include "xrt/xrt_defines.h"
#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_tracking.h"


xrt_result_t
twrap_slam_create_device(struct xrt_frame_context *xfctx,
                         enum xrt_device_name name,
                         struct xrt_slam_sinks **out_sinks,
                         struct xrt_device **out_device);
