// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to @ref drv_daydream.
 * @author Pete Black <pete.black@collabora.com>
 * @ingroup drv_daydream
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_daydream Daydream Controller driver
 * @ingroup drv
 *
 * @brief Driver for the Google Daydream Controller.
 */

/*!
 * Probing function for the Daydream controller.
 *
 * @ingroup drv_daydream
 */
struct xrt_auto_prober *
daydream_create_auto_prober();


/*!
 * @dir drivers/daydream
 *
 * @brief @ref drv_daydream files.
 */


#ifdef __cplusplus
}
#endif
