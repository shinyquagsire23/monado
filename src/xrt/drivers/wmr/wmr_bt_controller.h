// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Driver interface for Bluetooth based WMR motion controllers.
 * Note: Only tested with HP Reverb (G1) controllers that are manually
 * paired to a non hmd-integrated, generic BT usb adapter.
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */
#pragma once

#include "os/os_threading.h"
#include "math/m_imu_3dof.h"
#include "util/u_logging.h"
#include "xrt/xrt_device.h"

#include "wmr_controller_protocol.h"
#include "wmr_config.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Indices in input list of each input.
 */
enum wmr_bt_input_index
{
	WMR_INDEX_MENU_CLICK,
	WMR_INDEX_SQUEEZE_CLICK,
	WMR_INDEX_TRIGGER_VALUE,
	WMR_INDEX_THUMBSTICK_CLICK,
	WMR_INDEX_THUMBSTICK,
	WMR_INDEX_TRACKPAD_CLICK,
	WMR_INDEX_TRACKPAD_TOUCH,
	WMR_INDEX_TRACKPAD,
	WMR_INDEX_GRIP_POSE,
	WMR_INDEX_AIM_POSE,
};

/*!
 * A Bluetooth connected WMR Controller device, representing just a single controller.
 *
 * @ingroup drv_wmr
 * @implements xrt_device
 */
struct wmr_bt_controller
{
	struct xrt_device base;

	struct os_hid_device *controller_hid;
	struct os_thread_helper controller_thread;

	/* firmware configuration block */
	struct wmr_controller_config config;

	struct os_mutex lock;

	//! The last decoded package of IMU and button data
	struct wmr_controller_input input;
	//! Time of last IMU sample, in CPU time.
	uint64_t last_imu_timestamp_ns;
	//! Main fusion calculator.
	struct m_imu_3dof fusion;
	//! The last angular velocity from the IMU, for prediction.
	struct xrt_vec3 last_angular_velocity;

	enum u_logging_level log_level;
};


struct xrt_device *
wmr_bt_controller_create(struct os_hid_device *controller_hid,
                         enum xrt_device_type controller_type,
                         enum u_logging_level log_level);


#ifdef __cplusplus
}
#endif
