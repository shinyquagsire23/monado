// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief SteamVR driver device interface.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#pragma once


#ifdef __cplusplus
extern "C" {
#endif


struct xrt_device;

/*!
 * @defgroup drv_steamvr_lh Wrapper for the SteamVR Lighthouse driver.
 * @ingroup drv
 *
 * @brief Wrapper driver around the SteamVR Lighthouse driver.
 */

/*!
 * @dir drivers/steamvr_lh
 *
 * @brief @ref drv_steamvr_lh files.
 */

/*!
 * Create devices.
 *
 * @ingroup drv_steamvr_lh
 */
int
steamvr_lh_get_devices(struct xrt_device **out_xdevs);


#ifdef __cplusplus
}
#endif
