// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Public interface of Mercury hand tracking.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_tracking
 */
#pragma once
#include "xrt/xrt_defines.h"
#include "tracking/t_tracking.h"
#include "tracking/t_hand_tracking.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Create a Mercury hand tracking pipeline.
 *
 * @ingroup aux_tracking
 */
struct t_hand_tracking_sync *
t_hand_tracking_sync_mercury_create(struct t_stereo_camera_calibration *calib,
                                    struct t_camera_extra_info extra_camera_info,
                                    const char *models_folder);

#ifdef __cplusplus
} // extern "C"
#endif
