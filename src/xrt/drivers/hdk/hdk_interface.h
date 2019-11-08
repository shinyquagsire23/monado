// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to direct OSVR HDK driver code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_hdk
 */

#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup drv_hdk HDK driver
 * @ingroup drv
 *
 * @brief Driver for the OSVR HDK series of HMDs.
 */

#define HDK_VID 0x1532
#define HDK_PID 0x0b00

/*!
 * Probing function for HDK devices.
 *
 * @ingroup drv_hdk
 */
int
hdk_found(struct xrt_prober *xp,
          struct xrt_prober_device **devices,
          size_t num_devices,
          size_t index,
          cJSON *attached_data,
          struct xrt_device **out_xdev);

/*!
 * @dir drivers/hdk
 *
 * @brief @ref drv_hdk files.
 */


#ifdef __cplusplus
}
#endif
