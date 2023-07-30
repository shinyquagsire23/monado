/*
 * Copyright 2020 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 * OpenHMD - Free and Open Source API and drivers for immersive technology.
 */
/*!
 * @file
 * @brief  Oculus Rift S firmware parsing
 *
 * Functions for parsing JSON configuration from the HMD
 * and Touch Controller firmware.
 *
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */

/* Oculus Rift S Driver - firmware JSON parsing functions */
#include <string.h>
#include <stdio.h>

#include "util/u_json.h"
#include "util/u_misc.h"

#include "rift_s.h"
#include "rift_s_firmware.h"

#define JSON_INT(a, b, c) u_json_get_int(u_json_get(a, b), c)
#define JSON_FLOAT(a, b, c) u_json_get_float(u_json_get(a, b), c)
#define JSON_DOUBLE(a, b, c) u_json_get_double(u_json_get(a, b), c)
#define JSON_VEC3(a, b, c) u_json_get_vec3_array(u_json_get(a, b), c)
#define JSON_MATRIX_3X3_ARRAY(a, b, c) u_json_get_float_array(u_json_get(a, b), c.v, 9)
#define JSON_MATRIX_4x4_ARRAY(a, b, c) u_json_get_float_array(u_json_get(a, b), c.v, 16)

int
rift_s_parse_proximity_threshold(char *json_string, int *proximity_threshold)
{
	cJSON *json_root = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json_root)) {
		RIFT_S_ERROR("Could not parse JSON IMU calibration data.");
		cJSON_Delete(json_root);
		return -1;
	}

	if (!JSON_INT(json_root, "threshold", proximity_threshold))
		goto fail;

	cJSON_Delete(json_root);
	return 0;

fail:
	RIFT_S_WARN("Unrecognised Rift S Proximity Threshold JSON data.\n%s", json_string);
	cJSON_Delete(json_root);
	return -1;
}

static bool
check_file_format_version(cJSON *json_root, float expected_version, float *version_number)
{
	const cJSON *obj = u_json_get(json_root, "FileFormat");
	if (!cJSON_IsObject(json_root)) {
		return false;
	}

	const cJSON *version = u_json_get(obj, "Version");
	char *version_str = cJSON_GetStringValue(version);
	if (version_str == NULL) {
		return false;
	}

	*version_number = strtof(version_str, NULL);
	if (*version_number != expected_version)
		return false;

	return true;
}

int
rift_s_parse_imu_calibration(char *json_string, struct rift_s_imu_calibration *c)
{
	const cJSON *obj, *imu;
	float version_number = -1;

	cJSON *json_root = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json_root)) {
		RIFT_S_ERROR("Could not parse JSON IMU calibration data.");
		cJSON_Delete(json_root);
		return -1;
	}

	if (!check_file_format_version(json_root, 1.0, &version_number)) {
		goto fail;
	}

	imu = u_json_get(json_root, "ImuCalibration");
	if (!cJSON_IsObject(imu)) {
		goto fail;
	}

	if (!JSON_MATRIX_4x4_ARRAY(imu, "DeviceFromImu", c->device_from_imu))
		goto fail;

	/* Monado / Eigen expect column major 4x4 isometry, so transpose */
	math_matrix_4x4_transpose(&c->device_from_imu, &c->device_from_imu);

	obj = u_json_get(imu, "Gyroscope");
	if (!cJSON_IsObject(obj) || !JSON_MATRIX_3X3_ARRAY(obj, "RectificationMatrix", c->gyro.rectification)) {
		goto fail;
	}

	obj = u_json_get(obj, "Offset");
	if (!cJSON_IsObject(obj) || !JSON_VEC3(obj, "ConstantOffset", &c->gyro.offset)) {
		goto fail;
	}

	obj = u_json_get(imu, "Accelerometer");
	if (!cJSON_IsObject(obj) || !JSON_MATRIX_3X3_ARRAY(obj, "RectificationMatrix", c->accel.rectification)) {
		goto fail;
	}

	obj = u_json_get(obj, "Offset");
	if (!cJSON_IsObject(obj) || !JSON_VEC3(obj, "OffsetAtZeroDegC", &c->accel.offset_at_0C) ||
	    !JSON_VEC3(obj, "OffsetTemperatureCoefficient", &c->accel.temp_coeff)) {
		goto fail;
	}

	cJSON_Delete(json_root);
	return 0;

fail:
	RIFT_S_WARN("Unrecognised Rift S IMU Calibration JSON data. Version %f\n%s\n", version_number, json_string);
	cJSON_Delete(json_root);
	return -1;
}

static bool
rift_s_config_parse_camera_config(struct rift_s_camera_calibration *cam_config, int camera_id, const cJSON *camera_json)
{
	const cJSON *obj, *item;
	int id = -1;

	item = u_json_get(camera_json, "Id");
	if (item == NULL || (id = strtol(cJSON_GetStringValue(item), NULL, 10)) != camera_id) {
		RIFT_S_ERROR("Camera entry id %d doesn't match expected %d", id, camera_id);
		return false;
	}

	int camera_dims[2];

	obj = u_json_get(camera_json, "ImageSize");
	if (obj == NULL || u_json_get_int_array(obj, camera_dims, 2) != (size_t)2) {
		RIFT_S_ERROR("Missing/invalid camera ImageSize in camera %d", camera_id);
		return false;
	}

	cam_config->roi.extent.w = camera_dims[0];
	cam_config->roi.extent.h = camera_dims[1];

	/* Camera images are stacked horizontally in the received image */
	cam_config->roi.offset.w = camera_id * cam_config->roi.extent.w;
	cam_config->roi.offset.h = 0;

	if (!JSON_MATRIX_4x4_ARRAY(camera_json, "DeviceFromCamera", cam_config->device_from_camera)) {
		RIFT_S_ERROR("Missing/invalid camera DeviceFromCamera in camera %d", camera_id);
		return false;
	}

	/* Monado / Eigen expect column major 4x4 isometry, so transpose */
	math_matrix_4x4_transpose(&cam_config->device_from_camera, &cam_config->device_from_camera);

	obj = u_json_get(camera_json, "Projection");
	item = u_json_get(obj, "Model");
	if (item == NULL || strcmp(cJSON_GetStringValue(item), "PinholeSymmetric") != 0) {
		RIFT_S_ERROR("Missing/invalid camera projection model type %s in camera %d",
		             item != NULL ? cJSON_GetStringValue(item) : "NULL", camera_id);
		return false;
	}

	/* Projection coefficients f, cx, cy */
	float focal_params[3];

	item = u_json_get(obj, "Coefficients");
	if (item == NULL || u_json_get_float_array(item, focal_params, 3) != (size_t)3) {
		RIFT_S_ERROR("Missing/invalid camera projection coefficients in camera %d", camera_id);
		return false;
	}

	cam_config->projection.fx = cam_config->projection.fy = focal_params[0];
	cam_config->projection.cx = focal_params[1];
	cam_config->projection.cy = focal_params[2];

	/* Fisheye62 distortion */
	obj = u_json_get(camera_json, "Distortion");
	item = u_json_get(obj, "Model");
	if (item == NULL || strcmp(cJSON_GetStringValue(item), "Fisheye62") != 0) {
		RIFT_S_ERROR("Missing/invalid camera distortion model type %s in camera %d",
		             item != NULL ? cJSON_GetStringValue(item) : "NULL", camera_id);
		return false;
	}

	/* Projection coefficients k1, k2, k3, k4, k5, k6, p1, p2 */
	float dist_params[8];

	item = u_json_get(obj, "Coefficients");
	if (item == NULL || u_json_get_float_array(item, dist_params, 8) != (size_t)8) {
		RIFT_S_ERROR("Missing/invalid camera distortion coefficients in camera %d", camera_id);
		return false;
	}

	for (int i = 0; i < 6; i++) {
		cam_config->distortion.k[i] = dist_params[i];
	}
	cam_config->distortion.p1 = dist_params[6];
	cam_config->distortion.p2 = dist_params[7];

	return true;
}

int
rift_s_parse_camera_calibration_block(char *json_string, struct rift_s_camera_calibration_block *c)
{
	const cJSON *item;
	float version_number = -1;

	cJSON *json_root = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json_root)) {
		RIFT_S_ERROR("Could not parse JSON camera calibration data.");
		cJSON_Delete(json_root);
		return -1;
	}

	if (!check_file_format_version(json_root, 1.0, &version_number)) {
		goto fail;
	}

	cJSON *cameras = cJSON_GetObjectItemCaseSensitive(json_root, "CameraCalibration");
	if (!cJSON_IsArray(cameras)) {
		RIFT_S_ERROR("Cameras: not found or not an Array");
		return false;
	}

	int camera_id = 0;
	cJSON_ArrayForEach(item, cameras)
	{
		if (camera_id == RIFT_S_CAMERA_COUNT) {
			RIFT_S_ERROR("Too many camera calibration entries");
			goto fail;
		}

		if (!rift_s_config_parse_camera_config(c->cameras + camera_id, camera_id, item)) {
			goto fail;
		}

		camera_id++;
	}


	cJSON_Delete(json_root);
	return 0;

fail:
	RIFT_S_WARN("Unrecognised Rift S Camera Calibration JSON data. Version %f\n%s\n", version_number, json_string);
	cJSON_Delete(json_root);
	return -1;
}

static bool
json_read_led_point(const cJSON *led_model, struct rift_s_led *led, int n)
{
	const cJSON *array;
	double point[9];
	char name[32];

	snprintf(name, 32, "Point%d", n);
	array = u_json_get(led_model, name);
	if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != 9) {
		return false;
	}

	int j = 0;
	const cJSON *item = NULL;
	cJSON_ArrayForEach(item, array)
	{
		if (!cJSON_IsNumber(item)) {
			return false;
		}
		point[j++] = item->valuedouble;
	}

	led->pos.x = point[0];
	led->pos.y = point[1];
	led->pos.z = point[2];
	led->dir.x = point[3];
	led->dir.y = point[4];
	led->dir.z = point[5];
	led->angles.x = point[6];
	led->angles.y = point[7];
	led->angles.z = point[8];

	return true;
}

static bool
json_read_lensing_model(const cJSON *lensing_model, struct rift_s_lensing_model *model, int n)
{
	const cJSON *array;
	char name[32];

	snprintf(name, 32, "Model%d", n);
	array = u_json_get(lensing_model, name);
	if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != 5) {
		return false;
	}

	model->num_points = cJSON_GetArrayItem(array, 0)->valueint;

	for (int j = 0; j < 4; j++) {
		const cJSON *item = cJSON_GetArrayItem(array, j + 1);
		if (!cJSON_IsNumber(item)) {
			return false;
		}

		model->points[j] = item->valuedouble;
	}

	return true;
}

int
rift_s_controller_parse_imu_calibration(char *json_string, struct rift_s_controller_imu_calibration *c)
{
	const cJSON *obj, *version, *leds;
	const cJSON *item = NULL;
	int i;

	cJSON *json_root = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json_root)) {
		RIFT_S_ERROR("Could not parse JSON Controller IMU calibration data.");
		cJSON_Delete(json_root);
		return -1;
	}

	obj = u_json_get(json_root, "TrackedObject");
	if (!cJSON_IsObject(obj)) {
		goto fail;
	}

	version = u_json_get(obj, "FlsVersion");
	char *version_str = cJSON_GetStringValue(version);
	if (version_str == NULL || strcmp(version_str, "1.0.10")) {
		RIFT_S_ERROR("Controller calibration version number has changed - got %s", version_str);
		goto fail;
	}

	if (!JSON_VEC3(obj, "ImuPosition", &c->imu_position))
		goto fail;

	if (!JSON_MATRIX_4x4_ARRAY(obj, "AccCalibration", c->accel_calibration))
		goto fail;

	if (!JSON_MATRIX_4x4_ARRAY(obj, "GyroCalibration", c->gyro_calibration))
		goto fail;

	/* LED positions */
	leds = u_json_get(obj, "ModelPoints");
	if (!cJSON_IsObject(leds)) {
		goto fail;
	}

	c->num_leds = cJSON_GetArraySize(leds);
	c->leds = calloc(c->num_leds, sizeof(struct rift_s_led));
	i = 0;
	cJSON_ArrayForEach(item, leds)
	{
		if (!json_read_led_point(leds, c->leds + i, i))
			goto fail;
		i++;
	}

	/* LED lensing models */
	leds = u_json_get(obj, "Lensing");
	if (!cJSON_IsObject(leds)) {
		goto fail;
	}

	c->num_lensing_models = cJSON_GetArraySize(leds);
	c->lensing_models = calloc(c->num_lensing_models, sizeof(struct rift_s_lensing_model));
	i = 0;
	cJSON_ArrayForEach(item, leds)
	{
		if (!json_read_lensing_model(leds, c->lensing_models + i, i))
			goto fail;
	}

	if (!JSON_MATRIX_3X3_ARRAY(json_root, "gyro_m", c->gyro.rectification) ||
	    !JSON_VEC3(json_root, "gyro_b", &c->gyro.offset) ||
	    !JSON_MATRIX_3X3_ARRAY(json_root, "acc_m", c->accel.rectification) ||
	    !JSON_VEC3(json_root, "acc_b", &c->accel.offset)) {
		goto fail;
	}

	cJSON_Delete(json_root);
	return 0;

fail:
	RIFT_S_WARN("Unrecognised Rift S Controller Calibration JSON data.\n%s\n", json_string);
	rift_s_controller_free_imu_calibration(c);
	cJSON_Delete(json_root);
	return -1;
}

void
rift_s_controller_free_imu_calibration(struct rift_s_controller_imu_calibration *c)
{
	if (c->lensing_models) {
		free(c->lensing_models);
		c->lensing_models = NULL;
	}

	if (c->leds) {
		free(c->leds);
		c->leds = NULL;
	}
}
