// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to OpenHMD driver code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ohmd
 */

#pragma once

#include "math/m_api.h"
#include "xrt/xrt_device.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xrt_device *
oh_device_create(ohmd_context *ctx, ohmd_device *dev, const char *prod);

#define OHMD_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->ll, __VA_ARGS__)
#define OHMD_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->ll, __VA_ARGS__)
#define OHMD_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->ll, __VA_ARGS__)
#define OHMD_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->ll, __VA_ARGS__)
#define OHMD_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->ll, __VA_ARGS__)


#ifdef __cplusplus
}
#endif
