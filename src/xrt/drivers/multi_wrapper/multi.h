// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Combination of multiple @ref xrt_device.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup drv_multi
 */

#pragma once

#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_settings.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup drv_multi Multi device wrapper driver
 * @ingroup drv
 *
 * @brief Driver that can wrap multiple devices, for example to override tracking.
 */

/*!
 * Create a device that takes ownership of the target device and mimics it.
 *
 * Does not take ownership of the tracker device, one can be assigned to multiple targets.
 *
 * The pose provided by get_tracked_pose will be provided by the tracker device.
 *
 * @param override_type The kind of override this wrapper device will provide.
 * @param tracking_override_target An existing device that will be mimiced by the created device.
 * @param tracking_override_tracker An existing device that will be used to provide tracking data.
 * @param tracking_override_input_name The input name of the tracker device. XRT_INPUT_GENERIC_TRACKER_POSE for generic
 * trackers.
 * @param offset A static offset describing the real world transform from the "tracked point" of the target device to
 * the "tracked point" of the tracker device. A tracking sensors attached .1m above the HMD "center" sets y = 0.1.
 *
 * @ingroup drv_multi
 */
struct xrt_device *
multi_create_tracking_override(enum xrt_tracking_override_type override_type,
                               struct xrt_device *tracking_override_target,
                               struct xrt_device *tracking_override_tracker,
                               enum xrt_input_name tracking_override_input_name,
                               struct xrt_pose *offset);

#ifdef __cplusplus
}
#endif
