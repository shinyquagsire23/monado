// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Driver to emulate controllers from hand-tracking input
 * @author Moses Turner <moses@collabora.com>>
 *
 * @ingroup drv_cemu
 */

#pragma once
#include "xrt/xrt_device.h"

#ifdef __cplusplus
extern "C" {
#endif

int
cemu_devices_create(struct xrt_device *head, struct xrt_device *hands, struct xrt_device **out_xdevs);

#ifdef __cplusplus
}
#endif
