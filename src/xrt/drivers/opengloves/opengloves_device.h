// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGloves device.
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#pragma once
#include "util/u_logging.h"
#include "xrt/xrt_device.h"
#include "communication/opengloves_communication.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xrt_device *
opengloves_device_create(struct opengloves_communication_device *ocd, enum xrt_hand hand);

#ifdef __cplusplus
}
#endif
