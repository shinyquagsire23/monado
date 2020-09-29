// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  vive device header
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include "xrt/xrt_device.h"
#include "math/m_imu_3dof.h"
#include "os/os_threading.h"
#include "util/u_logging.h"
#include "util/u_distortion_mesh.h"

#include "vive_lighthouse.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * A lighthouse consisting of sensors.
 *
 * All sensors are placed in IMU space.
 */
struct lh_model
{
	struct lh_sensor *sensors;
	size_t num_sensors;
};

/*!
 * A single lighthouse senosor point and normal, in IMU space.
 */
struct lh_sensor
{
	struct xrt_vec3 pos;
	uint32_t _pad0;
	struct xrt_vec3 normal;
	uint32_t _pad1;
};

enum VIVE_VARIANT
{
	VIVE_UNKNOWN = 0,
	VIVE_VARIANT_VIVE,
	VIVE_VARIANT_PRO,
	VIVE_VARIANT_INDEX
};

/*!
 * @implements xrt_device
 */
struct vive_device
{
	struct xrt_device base;
	struct os_hid_device *mainboard_dev;
	struct os_hid_device *sensors_dev;
	struct os_hid_device *watchman_dev;

	struct lighthouse_watchman watchman;

	enum VIVE_VARIANT variant;

	struct os_thread_helper sensors_thread;
	struct os_thread_helper watchman_thread;
	struct os_thread_helper mainboard_thread;

	struct lh_model lh;

	struct
	{
		uint64_t time_ns;
		uint8_t sequence;
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

	struct
	{
		double lens_separation;
		double persistence;
		int eye_target_height_in_pixels;
		int eye_target_width_in_pixels;

		struct xrt_quat rot[2];

		//! Head position in tracking space.
		struct xrt_pose trackref;
		//! Head position in IMU space.
		struct xrt_pose imuref;
	} display;

	struct
	{
		uint16_t ipd;
		uint16_t lens_separation;
		uint16_t proximity;
		uint8_t button;
	} board;

	struct
	{
		uint32_t display_firmware_version;
		uint32_t firmware_version;
		uint8_t hardware_revision;
		uint8_t hardware_version_micro;
		uint8_t hardware_version_minor;
		uint8_t hardware_version_major;
		char mb_serial_number[32];
		char model_number[32];
		char device_serial_number[32];
	} firmware;

	struct xrt_quat rot_filtered;

	enum u_logging_level ll;
	bool disconnect_notified;

	struct
	{
		bool calibration;
		bool last;
	} gui;

	struct u_vive_values distortion[2];
};

struct vive_device *
vive_device_create(struct os_hid_device *mainboard_dev,
                   struct os_hid_device *sensors_dev,
                   struct os_hid_device *watchman_dev,
                   enum VIVE_VARIANT variant);

#ifdef __cplusplus
}
#endif
