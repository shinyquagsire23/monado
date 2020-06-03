// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to @ref drv_psvr.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_psvr
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_psvr PSVR driver
 * @ingroup drv
 *
 * @brief Driver for the Sony PSVR HMD.
 */

/*!
 * Create a probe for PSVR devices.
 *
 * @ingroup drv_psvr
 * @relates xrt_auto_prober
 */
struct xrt_auto_prober *
psvr_create_auto_prober(void);

/*!
 * @dir drivers/psvr
 *
 * @brief @ref drv_psvr files.
 */


#ifdef __cplusplus
}
#endif
