// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to OpenHMD driver code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ohmd
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_ohmd OpenHMD wrapper
 * @ingroup drv
 *
 * @brief Wrapper driver around OpenHMD.
 */

/*!
 * Create a probe for OpenHMD supported devices.
 *
 * @ingroup drv_ohmd
 */
struct xrt_auto_prober *
oh_create_auto_prober();

/*!
 * @dir drivers/ohmd
 *
 * @brief @ref drv_ohmd files.
 */


#ifdef __cplusplus
}
#endif
