// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to simulated driver.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_simulated
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_simulated Simulated driver
 * @ingroup drv
 *
 * @brief Simple do-nothing simulated driver.
 */

/*!
 * Create a auto prober for simulated devices.
 *
 * @ingroup drv_simulated
 */
struct xrt_auto_prober *
simulated_create_auto_prober(void);

/*!
 * Create a simulated hmd.
 *
 * @ingroup drv_simulated
 */
struct xrt_device *
simulated_hmd_create(void);

/*!
 * @dir drivers/simulated
 *
 * @brief @ref drv_simulated files.
 */


#ifdef __cplusplus
}
#endif
