// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  vive json header
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include <stdbool.h>

#include "xrt/xrt_defines.h"
#include "util/u_logging.h"
#include "util/u_distortion_mesh.h"

// public documentation
#define INDEX_MIN_IPD 0.058
#define INDEX_MAX_IPD 0.07

// arbitrary default values
#define DEFAULT_HAPTIC_FREQ 150.0f
#define MIN_HAPTIC_DURATION 0.05f

enum VIVE_VARIANT
{
	VIVE_UNKNOWN = 0,
	VIVE_VARIANT_VIVE,
	VIVE_VARIANT_PRO,
	VIVE_VARIANT_INDEX
};

enum VIVE_CONTROLLER_VARIANT
{
	CONTROLLER_VIVE_WAND,
	CONTROLLER_INDEX_LEFT,
	CONTROLLER_INDEX_RIGHT,
	CONTROLLER_TRACKER_GEN1,
	CONTROLLER_TRACKER_GEN2,
	CONTROLLER_UNKNOWN
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

struct vive_config
{
	//! log level accessed by the config parser
	enum u_logging_level ll;

	enum VIVE_VARIANT variant;

	struct
	{
		double acc_range;
		double gyro_range;
		struct xrt_vec3 acc_bias;
		struct xrt_vec3 acc_scale;
		struct xrt_vec3 gyro_bias;
		struct xrt_vec3 gyro_scale;

		//! IMU position in tracking space.
		struct xrt_pose trackref;
	} imu;

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

	struct u_vive_values distortion[2];

	struct lh_model lh;
};

struct vive_controller_config
{
	enum u_logging_level ll;

	enum VIVE_CONTROLLER_VARIANT variant;

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

	struct
	{
		double acc_range;
		double gyro_range;
		struct xrt_vec3 acc_bias;
		struct xrt_vec3 acc_scale;
		struct xrt_vec3 gyro_bias;
		struct xrt_vec3 gyro_scale;

		//! IMU position in tracking space.
		struct xrt_pose trackref;
	} imu;
};

bool
vive_config_parse(struct vive_config *d, char *json_string);


struct vive_controller_device;

bool
vive_config_parse_controller(struct vive_controller_config *d, char *json_string);
