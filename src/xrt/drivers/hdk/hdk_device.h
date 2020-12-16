// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to direct OSVR HDK driver code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_hdk
 */

#pragma once

#include "os/os_threading.h"
#include "util/u_logging.h"

#ifdef __cplusplus
extern "C" {
#endif

enum HDK_VARIANT
{
	HDK_UNKNOWN = 0,
	HDK_VARIANT_1_2,
	HDK_VARIANT_1_3_1_4,
	HDK_VARIANT_2
};

/*!
 * @implements xrt_device
 */
struct hdk_device
{
	struct xrt_device base;
	struct os_hid_device *dev;
	enum HDK_VARIANT variant;

	struct os_thread_helper imu_thread;

	enum u_logging_level ll;
	bool disconnect_notified;

	struct xrt_quat quat;
	struct xrt_quat ang_vel_quat;
	bool quat_valid;
};

static inline struct hdk_device *
hdk_device(struct xrt_device *xdev)
{
	return (struct hdk_device *)xdev;
}

struct hdk_device *
hdk_device_create(struct os_hid_device *dev, enum HDK_VARIANT variant);


/*
 *
 * Printing functions.
 *
 */

#define HDK_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->ll, __VA_ARGS__)
#define HDK_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->ll, __VA_ARGS__)
#define HDK_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->ll, __VA_ARGS__)
#define HDK_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->ll, __VA_ARGS__)
#define HDK_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->ll, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
