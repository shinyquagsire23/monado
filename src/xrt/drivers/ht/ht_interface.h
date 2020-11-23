// Copyright 2029, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to camera based hand tracking driver code.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

#include "math/m_api.h"
#include "xrt/xrt_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_ht Camera based hand tracking
 * @ingroup drv
 *
 * @brief
 */

/*!
 * Create a probe for camera based hand tracking.
 *
 * @ingroup drv_ht
 */
struct xrt_auto_prober *
ht_create_auto_prober();

/*!
 * @dir drivers/handtracking
 *
 * @brief @ref drv_ht files.
 */
#ifdef __cplusplus
}
#endif
