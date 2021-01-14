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

enum controller_variant
{
	CONTROLLER_VIVE_WAND,
	CONTROLLER_INDEX_LEFT,
	CONTROLLER_INDEX_RIGHT,
	CONTROLLER_TRACKER_GEN1,
	CONTROLLER_TRACKER_GEN2,
	CONTROLLER_UNKNOWN
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

	struct
	{
		uint64_t time_ns;
		uint32_t last_sample_time_raw;
		double acc_range;
		double gyro_range;
		struct xrt_vec3 acc_bias;
		struct xrt_vec3 acc_scale;
		struct xrt_vec3 gyro_bias;
		struct xrt_vec3 gyro_scale;

		//! IMU position in tracking space.
		struct xrt_pose trackref;
	} imu;

	struct m_imu_3dof fusion;

	struct
	{
		struct xrt_vec3 acc;
		struct xrt_vec3 gyro;
	} last;

	struct xrt_quat rot_filtered;

	enum u_logging_level ll;

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

	struct
	{
		uint32_t firmware_version;
		uint8_t hardware_revision;
		uint8_t hardware_version_micro;
		uint8_t hardware_version_minor;
		uint8_t hardware_version_major;
		char mb_serial_number[32];
		char model_number[32];
		char device_serial_number[32];
	} firmware;

	enum watchman_gen watchman_gen;
	enum controller_variant variant;

	struct u_hand_tracking hand_tracking;
};

struct vive_controller_device *
vive_controller_create(struct os_hid_device *controller_hid, enum watchman_gen watchman_gen, int controller_num);

#ifdef __cplusplus
}
#endif
