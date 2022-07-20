// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to @ref drv_vive
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include <stdlib.h>
#include <stdint.h>

#include "xrt/xrt_device.h"
#include "os/os_threading.h"
#include "math/m_imu_3dof.h"
#include "util/u_logging.h"
#include "util/u_hand_tracking.h"
#include "vive/vive_config.h"


#ifdef __cplusplus
extern "C" {
#endif
/*!
 * @ingroup drv_vive
 *
 * @brief Driver for the HTC Vive and Valve Index controllers.
 */

enum watchman_gen
{
	WATCHMAN_GEN1,
	WATCHMAN_GEN2,
	WATCHMAN_GEN_UNKNOWN
};

/*!
 * A Vive Controller device, representing just a single controller.
 *
 * @ingroup drv_vive
 * @implements xrt_device
 */
struct vive_controller_device
{
	struct xrt_device base;

	struct os_hid_device *controller_hid;
	struct os_thread_helper controller_thread;
	struct os_mutex lock;

	struct
	{
		timepoint_ns last_sample_ts_ns;
		uint32_t last_sample_ticks;
		timepoint_ns ts_received_ns;
	} imu;

	struct m_imu_3dof fusion;

	struct
	{
		struct xrt_vec3 acc;
		struct xrt_vec3 gyro;
	} last;

	struct xrt_quat rot_filtered;

	enum u_logging_level log_level;

	uint32_t last_ticks;

	//! Which vive controller in the system are we?
	size_t index;

	struct
	{
		struct xrt_vec2 trackpad;
		float trigger;
		uint8_t buttons;
		uint8_t last_buttons;

		uint8_t touch;
		uint8_t last_touch;

		uint8_t middle_finger_handle;
		uint8_t ring_finger_handle;
		uint8_t pinky_finger_handle;
		uint8_t index_finger_trigger;

		uint8_t squeeze_force;
		uint8_t trackpad_force;

		bool charging;
		uint8_t battery;
	} state;

	enum watchman_gen watchman_gen;

	struct u_hand_tracking hand_tracking;

	struct vive_controller_config config;
};

struct vive_controller_device *
vive_controller_create(struct os_hid_device *controller_hid, enum watchman_gen watchman_gen, int controller_num);

#ifdef __cplusplus
}
#endif
