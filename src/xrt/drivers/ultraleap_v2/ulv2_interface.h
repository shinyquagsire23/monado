// Copyright 2020-2021, Collabora, Ltd.
// Copyright 2020-2021, Moses Turner.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief   Driver for Ultraleap's V2 API for the Leap Motion Controller.
 * @author  Moses Turner <mosesturner@protonmail.com>
 * @author  Christoph Haag <christoph.haag@collabora.com>
 * @ingroup drv_ulv2
 */

#pragma once

#include "math/m_api.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_prober.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ULV2_VID 0xf182
#define ULV2_PID 0x0003

/*!
 * @defgroup drv_ulv2 Leap Motion Controller driver
 * @ingroup drv
 *
 * @brief Leap Motion Controller driver using Ultraleap's V2 API
 */

/*!
 * Probing function for Leap Motion Controller.
 *
 * @ingroup drv_ulv2
 * @see xrt_prober_found_func_t
 */
xrt_result_t
ulv2_create_device(struct xrt_device **out_xdev);
#ifdef __cplusplus
}
#endif
