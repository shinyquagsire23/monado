// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to the WMR driver.
 * @author nima01 <nima_zero_one@protonmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_wmr
 */

#pragma once


#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_wmr Windows Mixed Reality driver
 * @ingroup drv
 *
 * @brief Windows Mixed Reality driver.
 */

/*!
 * Probing function for Windows Mixed Reality devices.
 *
 * @ingroup drv_wmr
 * @see xrt_prober_found_func_t
 */
int
wmr_found(struct xrt_prober *xp,
          struct xrt_prober_device **devices,
          size_t device_count,
          size_t index,
          cJSON *attached_data,
          struct xrt_device **out_xdev);


/*!
 * Probing function for Bluetooth WMR motion controllers.
 *
 * @ingroup drv_wmr
 */
int
wmr_bt_controller_found(struct xrt_prober *xp,
                        struct xrt_prober_device **devices,
                        size_t device_count,
                        size_t index,
                        cJSON *attached_data,
                        struct xrt_device **out_xdev);


/*!
 * @dir drivers/wmr
 *
 * @brief @ref drv_wmr files.
 */


#ifdef __cplusplus
}
#endif
