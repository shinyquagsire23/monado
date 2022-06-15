// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Public interface of old rgb hand tracking.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_tracking
 */

#include "tracking/t_tracking.h"
#include "tracking/t_hand_tracking.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Create a old style RGB hand tracking pipeline.
 *
 * @ingroup aux_tracking
 */
struct t_hand_tracking_sync *
t_hand_tracking_sync_old_rgb_create(struct t_stereo_camera_calibration *calib);


#ifdef __cplusplus
} // extern "C"
#endif
