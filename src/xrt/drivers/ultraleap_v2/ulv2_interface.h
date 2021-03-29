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

int
ulv2_found(struct xrt_prober *xp,
           struct xrt_prober_device **devices,
           size_t num_devices,
           size_t index,
           cJSON *attached_data,
           struct xrt_device **out_xdev);

#ifdef __cplusplus
}
#endif
