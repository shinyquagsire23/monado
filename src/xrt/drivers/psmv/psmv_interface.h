// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to @ref drv_psmv.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_psmv
 */

#pragma once

#include "xrt/xrt_compiler.h"


#ifdef __cplusplus
extern "C" {
#endif


struct xrt_device;
struct xrt_tracked_psmv;

/*!
 * @defgroup drv_psmv PS Move driver
 * @ingroup drv
 *
 * @brief Driver for the Sony PlayStation Move Controller.
 */


#define PSMV_VID 0x054c
#define PSMV_PID_ZCM1 0x03d5
#define PSMV_PID_ZCM2 0x0c5e

/*!
 * Function to create a PSMV device.
 *
 * @ingroup drv_psmv
 * @see xrt_builder
 */
struct xrt_device *
psmv_device_create(struct xrt_prober *xp, struct xrt_prober_device *xpdev, struct xrt_tracked_psmv *tracker);

/*!
 * Probing function for the PS Move devices.
 *
 * @ingroup drv_psmv
 * @see xrt_prober_found_func_t
 */
int
psmv_found(struct xrt_prober *xp,
           struct xrt_prober_device **devices,
           size_t device_count,
           size_t index,
           cJSON *attached_data,
           struct xrt_device **out_xdevs);

/*!
 * @dir drivers/psmv
 *
 * @brief @ref drv_psmv files.
 */


#ifdef __cplusplus
}
#endif
