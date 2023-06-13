// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to @ref drv_vive.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include <stdlib.h>

#include "xrt/xrt_prober.h"

#include "vive_device.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup drv_vive HTC Vive and Valve Index driver
 * @ingroup drv
 *
 * @brief Driver for the HTC Vive and Valve Index family of HMDs.
 */

/*!
 * Probing function for Vive devices.
 *
 * @ingroup drv_vive
 * @see xrt_prober_found_func_t
 */
int
vive_found(struct xrt_prober *xp,
           struct xrt_prober_device **devices,
           size_t device_count,
           size_t index,
           cJSON *attached_data,
           struct vive_tracking_status tstatus,
           struct vive_source *vs,
           struct vive_config **out_vive_config,
           struct xrt_device **out_xdev);


/*!
 * Probing function for HTC Vive and Valve Index controllers.
 *
 * @ingroup drv_vive
 * @see xrt_prober_found_func_t
 */
int
vive_controller_found(struct xrt_prober *xp,
                      struct xrt_prober_device **devices,
                      size_t device_count,
                      size_t index,
                      cJSON *attached_data,
                      struct xrt_device **out_xdevs);

/*!
 * @dir drivers/vive
 *
 * @brief @ref drv_vive files.
 */


#ifdef __cplusplus
}
#endif
