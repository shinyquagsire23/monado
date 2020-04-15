// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to @ref drv_vive
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
/*!
 * @ingroup drv_vive
 *
 * @brief Driver for the HTC Vive and Valve Index controllers.
 */

#define VALVE_VID 0x28de
#define VIVE_WATCHMAN_DONGLE 0x2101
#define VIVE_WATCHMAN_DONGLE_GEN2 0x2102

/*!
 * Probing function for HTC Vive and Valve Index devices.
 *
 * @ingroup drv_vive
 */
int
vive_controller_found(struct xrt_prober *xp,
                      struct xrt_prober_device **devices,
                      size_t num_devices,
                      size_t index,
                      cJSON *attached_data,
                      struct xrt_device **out_xdevs);

#ifdef __cplusplus
}
#endif
