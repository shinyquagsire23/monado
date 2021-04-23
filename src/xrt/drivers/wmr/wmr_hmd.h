// Copyright 2018, Philipp Zabel.
// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to the WMR HMD driver code.
 * @author Philipp Zabel <philipp.zabel@gmail.com>
 * @author nima01 <nima_zero_one@protonmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_wmr
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_prober.h"
#include "os/os_threading.h"
#include "math/m_imu_3dof.h"
#include "util/u_logging.h"

#include "wmr_protocol.h"


#ifdef __cplusplus
extern "C" {
#endif


enum rvb_g1_status_bits
{
	// clang-format off
	REVERB_G1_STATUS_BIT_UNKNOWN_BIT_0 = (1 << 0),
	REVERB_G1_STATUS_BIT_UNKNOWN_BIT_1 = (1 << 1),
	REVERB_G1_STATUS_BIT_UNKNOWN_BIT_2 = (1 << 2),
	REVERB_G1_STATUS_BIT_UNKNOWN_BIT_3 = (1 << 3),
	REVERB_G1_STATUS_BIT_UNKNOWN_BIT_4 = (1 << 4),
	REVERB_G1_STATUS_BIT_UNKNOWN_BIT_5 = (1 << 5),
	REVERB_G1_STATUS_BIT_UNKNOWN_BIT_6 = (1 << 6),
	REVERB_G1_STATUS_BIT_UNKNOWN_BIT_7 = (1 << 7),
	// clang-format on
};

/*!
 * @implements xrt_device
 */
struct wmr_hmd
{
	struct xrt_device base;

	//! Packet reading thread.
	struct os_thread_helper oth;

	enum u_logging_level log_level;

	/*!
	 * This is the hololens sensor device, this is were we get all of the
	 * IMU data and read the config from.
	 *
	 * During start it is owned by the thread creating the device, after
	 * init it is owned by the reading thread, there is no mutex protecting
	 * this field as it's only used by the reading thread in @p oth.
	 */
	struct os_hid_device *hid_hololens_senors_dev;
	struct os_hid_device *hid_control_dev;

	//! Latest raw IPD value from the device.
	uint16_t raw_ipd;

	struct hololens_sensors_packet packet;

	struct xrt_vec3 raw_accel;
	struct xrt_vec3 raw_gyro;

	struct
	{
		//! Protects all members of the `fusion` substruct.
		struct os_mutex mutex;

		//! Main fusion calculator.
		struct m_imu_3dof i3dof;

		//! The last angular velocity from the IMU, for prediction.
		struct xrt_vec3 last_angular_velocity;

		//! When did we get the last IMU sample, in CPU time.
		uint64_t last_imu_timestamp_ns;
	} fusion;

	struct
	{
		bool fusion;
		bool misc;
	} gui;
};

static inline struct wmr_hmd *
wmr_hmd(struct xrt_device *p)
{
	return (struct wmr_hmd *)p;
}

struct xrt_device *
wmr_hmd_create(struct os_hid_device *hid_holo, struct os_hid_device *hid_ctrl, enum u_logging_level ll);

#define WMR_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->log_level, __VA_ARGS__)
#define WMR_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->log_level, __VA_ARGS__)
#define WMR_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->log_level, __VA_ARGS__)
#define WMR_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->log_level, __VA_ARGS__)
#define WMR_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->log_level, __VA_ARGS__)


#ifdef __cplusplus
}
#endif
