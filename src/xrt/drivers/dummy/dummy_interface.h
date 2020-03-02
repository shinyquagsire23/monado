// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to dummy driver.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_dummy
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_dummy Dummy driver
 * @ingroup drv
 *
 * @brief Simple do-nothing dummy driver.
 */

/*!
 * Create a auto prober for dummy devices.
 *
 * @ingroup drv_dummy
 */
struct xrt_auto_prober *
dummy_create_auto_prober(void);

/*!
 * Create a dummy hmd.
 *
 * @ingroup drv_dummy
 */
struct xrt_device *
dummy_hmd_create(void);

/*!
 * @dir drivers/dummy
 *
 * @brief @ref drv_dummy files.
 */


#ifdef __cplusplus
}
#endif
