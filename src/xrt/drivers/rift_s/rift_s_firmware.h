/*
 * Copyright 2020 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 * OpenHMD - Free and Open Source API and drivers for immersive technology.
 */
/*!
 * @file
 * @brief  Oculus Rift S firmware parsing interface
 *
 * Functions for parsing JSON configuration from the HMD
 * and Touch Controller firmware.
 *
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */
#ifndef __RTFT_S_FIRMWARE__
#define __RTFT_S_FIRMWARE__

#include "math/m_mathinclude.h"
#include "math/m_api.h"

enum rift_s_firmware_block
{
	RIFT_S_FIRMWARE_BLOCK_SERIAL_NUM = 0x0B,
	RIFT_S_FIRMWARE_BLOCK_THRESHOLD = 0xD,
	RIFT_S_FIRMWARE_BLOCK_IMU_CALIB = 0xE,
	RIFT_S_FIRMWARE_BLOCK_CAMERA_CALIB = 0xF,
	RIFT_S_FIRMWARE_BLOCK_DISPLAY_COLOR_CALIB = 0x10,
	RIFT_S_FIRMWARE_BLOCK_LENS_CALIB = 0x12
};

enum rift_s_camera_id
{
	RIFT_S_CAMERA_TOP = 0x0,
	RIFT_S_CAMERA_SIDE_LEFT = 0x1,
	RIFT_S_CAMERA_FRONT_RIGHT = 0x2,
	RIFT_S_CAMERA_FRONT_LEFT = 0x3,
	RIFT_S_CAMERA_SIDE_RIGHT = 0x4,
	RIFT_S_CAMERA_COUNT,
};

//! Order/index of cameras when dealing with multi-camera tracking
static const enum rift_s_camera_id CAM_IDX_TO_ID[RIFT_S_CAMERA_COUNT] = {
    RIFT_S_CAMERA_FRONT_LEFT, RIFT_S_CAMERA_FRONT_RIGHT, //
    RIFT_S_CAMERA_SIDE_LEFT,  RIFT_S_CAMERA_SIDE_RIGHT,  //
    RIFT_S_CAMERA_TOP,
};

struct rift_s_imu_calibration
{
	struct xrt_matrix_4x4 device_from_imu;

	struct
	{
		struct xrt_matrix_3x3 rectification;
		struct xrt_vec3 offset;
	} gyro;

	struct
	{
		struct xrt_matrix_3x3 rectification;
		struct xrt_vec3 offset_at_0C;
		struct xrt_vec3 temp_coeff;
	} accel;
};

struct rift_s_projection_pinhole
{
	float cx, cy; /* Principal point */
	float fx, fy; /* Focal length */
};

struct rift_s_fisheye62_distortion
{
	float k[6];   /* Radial distortion coefficients */
	float p2, p1; /* Tangential distortion parameters */
};

struct rift_s_camera_calibration
{
	struct xrt_rect roi;
	struct xrt_matrix_4x4 device_from_camera;

	struct rift_s_projection_pinhole projection;
	struct rift_s_fisheye62_distortion distortion;
};

struct rift_s_camera_calibration_block
{
	struct rift_s_camera_calibration cameras[RIFT_S_CAMERA_COUNT];
};

/* Rift S controller LED entry */
struct rift_s_led
{
	// Relative position in metres
	struct xrt_vec3 pos;
	// Normal
	struct xrt_vec3 dir;

	// 85.0, 80.0, 0.0 in all entries so far
	struct xrt_vec3 angles;
};

struct rift_s_lensing_model
{
	int num_points;
	float points[4];
};

struct rift_s_controller_imu_calibration
{
	struct
	{
		struct xrt_matrix_3x3 rectification;
		struct xrt_vec3 offset;
	} gyro;

	struct
	{
		struct xrt_matrix_3x3 rectification;
		struct xrt_vec3 offset;
	} accel;

	struct xrt_vec3 imu_position;

	uint8_t num_leds;
	struct rift_s_led *leds;

	/* For some reason we have a separate calibration
	 * 4x4 matrix on top of the separate rectification
	 * and offset for gyro and accel
	 */
	struct xrt_matrix_4x4 gyro_calibration;
	struct xrt_matrix_4x4 accel_calibration;

	/* Lensing models */
	int num_lensing_models;
	struct rift_s_lensing_model *lensing_models;
};

int
rift_s_parse_proximity_threshold(char *json, int *proximity_threshold);
int
rift_s_parse_imu_calibration(char *json, struct rift_s_imu_calibration *c);
int
rift_s_parse_camera_calibration_block(char *json, struct rift_s_camera_calibration_block *c);
int
rift_s_controller_parse_imu_calibration(char *json, struct rift_s_controller_imu_calibration *c);
void
rift_s_controller_free_imu_calibration(struct rift_s_controller_imu_calibration *c);

#endif
