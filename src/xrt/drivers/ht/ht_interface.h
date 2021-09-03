// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to camera based hand tracking driver code.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

#include "math/m_api.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "vive/vive_config.h"

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
 * Create a hand tracker device.
 *
 * @ingroup drv_ht
 */
struct xrt_device *
ht_device_create(struct xrt_prober *xp, struct t_stereo_camera_calibration *calib);

/*!
 * @dir drivers/ht
 *
 * @brief @ref drv_ht files.
 */
#ifdef __cplusplus
}
#endif
