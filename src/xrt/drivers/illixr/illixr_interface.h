// Copyright 2020-2021, The Board of Trustees of the University of Illinois.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  ILLIXR driver interface
 * @author RSIM Group <illixr@cs.illinois.edu>
 * @ingroup drv_illixr
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_illixr illixr driver.
 * @ingroup drv
 *
 * @brief illixr driver.
 */

/*!
 * Create a auto prober for illixr devices.
 *
 * @ingroup drv_illixr
 */
struct xrt_auto_prober *
illixr_create_auto_prober(void);

/*!
 * Create a illixr hmd.
 *
 * @ingroup drv_illixr
 */
struct xrt_device *
illixr_hmd_create(const char *path, const char *comp);

/*!
 * @dir drivers/illixr
 *
 * @brief @ref drv_illixr files.
 */


#ifdef __cplusplus
}
#endif
