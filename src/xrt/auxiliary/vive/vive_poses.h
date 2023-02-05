// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  vive poses header
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_vive
 */

#pragma once

#include "xrt/xrt_defines.h"
#include "vive_config.h"

void
vive_poses_get_pose_offset(enum xrt_device_name device_name,
                           enum xrt_device_type device_type,
                           enum xrt_input_name input_name,
                           struct xrt_pose *out_offset_pose);
