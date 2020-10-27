// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Android sensors driver header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_android
 */

#pragma once

#include <android/sensor.h>

#include "math/m_api.h"
#include "math/m_imu_pre.h"
#include "math/m_imu_3dof.h"

#include "xrt/xrt_device.h"

#include "os/os_threading.h"

#include "util/u_logging.h"
#include "util/u_distortion.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @implements xrt_device
 */
struct android_device
{
	struct xrt_device base;
	struct os_thread_helper oth;

	ASensorManager *sensor_manager;
	const ASensor *accelerometer;
	const ASensor *gyroscope;
	ASensorEventQueue *event_queue;
	struct u_cardboard_distortion cardboard;


	struct
	{
		//! Lock for last and fusion.
		struct os_mutex lock;
		struct m_imu_3dof fusion;
	};

	enum u_logging_level ll;
};


struct android_device *
android_device_create();


/*
 *
 * Printing functions.
 *
 */

#define ANDROID_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->ll, __VA_ARGS__)
#define ANDROID_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->ll, __VA_ARGS__)
#define ANDROID_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->ll, __VA_ARGS__)
#define ANDROID_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->ll, __VA_ARGS__)
#define ANDROID_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->ll, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
