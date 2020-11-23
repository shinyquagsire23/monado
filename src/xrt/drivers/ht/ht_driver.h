// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to camera based hand tracking driver code.
 * @author Christtoph Haag <christtoph.haag@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

#include "math/m_api.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_prober.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xrt_device *
ht_device_create();


#ifdef __cplusplus
}
#endif
