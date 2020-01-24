// Copyright 2019, Collabora, Ltd.
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
 * @defgroup drv_ns North Star Driver
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
ns_hmd_create(bool print_spew, bool print_debug);

/*!
 * @dir drivers/ns
 *
 * @brief @ref drv_ns files.
 */


#ifdef __cplusplus
}
#endif
