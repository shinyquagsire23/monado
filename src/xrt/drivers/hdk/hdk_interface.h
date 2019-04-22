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

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup drv_hdk HDK Driver
 * @ingroup drv
 *
 * @brief Driver for the HDK HMD.
 */

/*!
 * Probe for HDKs.
 *
 * @ingroup drv_hdk
 */
struct xrt_auto_prober*
hdk_create_auto_prober();

/*!
 * @dir drivers/hdk
 *
 * @brief @ref drv_hdk files.
 */


#ifdef __cplusplus
}
#endif
