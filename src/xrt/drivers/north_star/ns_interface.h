// Copyright 2019-2020, Collabora, Ltd.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to North Star driver code.
 * @author Nova King <technobaboo@gmail.com>
 * @ingroup drv_ns
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup drv_ns North Star driver
 * @ingroup drv
 *
 * @brief Driver for the North Star HMD.
 */

/*!
 * Create a probe for NS devices.
 *
 * @ingroup drv_ns
 */
struct xrt_auto_prober *
ns_create_auto_prober(void);

/*!
 * Create a North Star hmd.
 *
 * @ingroup drv_ns
 */
struct xrt_device *
ns_hmd_create(const char *config_path);

/*!
 * @dir drivers/north_star
 *
 * @brief @ref drv_ns files.
 */


#ifdef __cplusplus
}
#endif
