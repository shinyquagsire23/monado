// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vive json implementation
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#include <stdio.h>

#include "vive_config.h"

#include "util/u_json.h"
#include "math/m_api.h"

#include "vive_device.h"
#include "vive_controller.h"

#define JSON_INT(a, b, c) u_json_get_int(u_json_get(a, b), c)
#define JSON_FLOAT(a, b, c) u_json_get_float(u_json_get(a, b), c)
#define JSON_DOUBLE(a, b, c) u_json_get_double(u_json_get(a, b), c)
#define JSON_VEC3(a, b, c) u_json_get_vec3_array(u_json_get(a, b), c)
#define JSON_MATRIX_3X3(a, b, c) u_json_get_matrix_3x3(u_json_get(a, b), c)
#define JSON_STRING(a, b, c)                                                   \
	u_json_get_string_into_array(u_json_get(a, b), c, sizeof(c))

static void
_get_color_coeffs(struct xrt_hmd_parts *hmd,
                  const cJSON *coeffs,
                  uint8_t eye,
                  uint8_t channel)
{
	// this is 4 on index, all values populated
	// assert(coeffs->length == 8);
	// only 3 coeffs contain values
	const cJSON *item = NULL;
	size_t i = 0;
	cJSON_ArrayForEach(item, coeffs)
	{
		hmd->distortion.vive.coefficients[eye][i][channel] =
		    (float)item->valuedouble;
		++i;
		if (i == 3) {
			break;
		}
	}
}

static void
_get_color_coeffs_lookup(struct xrt_hmd_parts *hmd,
                         const cJSON *eye_json,
                         const char *name,
                         uint8_t eye,
                         uint8_t channel)
{
	const cJSON *distortion =
	    cJSON_GetObjectItemCaseSensitive(eye_json, name);
	if (distortion == NULL) {
		return;
	}

	const cJSON *coeffs =
	    cJSON_GetObjectItemCaseSensitive(distortion, "coeffs");
	if (coeffs == NULL) {
		return;
	}

	_get_color_coeffs(hmd, coeffs, eye, channel);
}

static void
_get_pose_from_pos_x_z(const cJSON *obj, struct xrt_pose *pose)
{
	struct xrt_vec3 plus_x, plus_z;
	JSON_VEC3(obj, "plus_x", &plus_x);
	JSON_VEC3(obj, "plus_z", &plus_z);
	JSON_VEC3(obj, "position", &pose->position);

	math_quat_from_plus_x_z(&plus_x, &plus_z, &pose->orientation);
}

static void
_get_distortion_properties(struct vive_device *d,
                           const cJSON *eye_transform_json,
                           uint8_t eye)
{
	struct xrt_hmd_parts *hmd = d->base.hmd;

	const cJSON *eye_json = cJSON_GetArrayItem(eye_transform_json, eye);
	if (eye_json == NULL) {
		return;
	}

	struct xrt_matrix_3x3 rot = {0};
	if (JSON_MATRIX_3X3(eye_json, "eye_to_head", &rot)) {
		math_quat_from_matrix_3x3(&rot, &d->display.rot[eye]);
	}

	// TODO: store grow_for_undistort per eye
	// clang-format off
	JSON_FLOAT(eye_json, "grow_for_undistort", &hmd->distortion.vive.grow_for_undistort);
	JSON_FLOAT(eye_json, "undistort_r2_cutoff", &hmd->distortion.vive.undistort_r2_cutoff[eye]);
	// clang-format on

	const cJSON *distortion =
	    cJSON_GetObjectItemCaseSensitive(eye_json, "distortion");
	if (distortion != NULL) {
		// TODO: store center per color
		// clang-format off
		JSON_FLOAT(eye_json, "center_x", &hmd->distortion.vive.center[eye][0]);
		JSON_FLOAT(eye_json, "center_y", &hmd->distortion.vive.center[eye][1]);
		// clang-format on

		// green
		const cJSON *coeffs =
		    cJSON_GetObjectItemCaseSensitive(distortion, "coeffs");
		if (coeffs != NULL) {
			_get_color_coeffs(hmd, coeffs, eye, 1);
		}
	}

	_get_color_coeffs_lookup(hmd, eye_json, "distortion_red", eye, 0);
	_get_color_coeffs_lookup(hmd, eye_json, "distortion_blue", eye, 2);
}

static void
_get_lighthouse(struct vive_device *d, const cJSON *json)
{
	const cJSON *lh =
	    cJSON_GetObjectItemCaseSensitive(json, "lighthouse_config");
	if (lh == NULL) {
		return;
	}

	const cJSON *json_map =
	    cJSON_GetObjectItemCaseSensitive(lh, "channelMap");
	const cJSON *json_normals =
	    cJSON_GetObjectItemCaseSensitive(lh, "modelNormals");
	const cJSON *json_points =
	    cJSON_GetObjectItemCaseSensitive(lh, "modelPoints");

	if (json_map == NULL || json_normals == NULL || json_points == NULL) {
		return;
	}

	size_t map_size = cJSON_GetArraySize(json_map);
	size_t normals_size = cJSON_GetArraySize(json_normals);
	size_t points_size = cJSON_GetArraySize(json_points);

	if (map_size != normals_size || normals_size != points_size ||
	    map_size <= 0) {
		return;
	}

	uint32_t *map = U_TYPED_ARRAY_CALLOC(uint32_t, map_size);
	struct lh_sensor *s = U_TYPED_ARRAY_CALLOC(struct lh_sensor, map_size);

	size_t i = 0;
	const cJSON *item = NULL;
	cJSON_ArrayForEach(item, json_map)
	{
		// Build the channel map
		int map_item = 0;
		u_json_get_int(item, &map_item);
		map[i++] = (uint32_t)map_item;
	}

	i = 0;
	item = NULL;
	cJSON_ArrayForEach(item, json_normals)
	{
		// Store in channel map order.
		u_json_get_vec3_array(item, &s[map[i++]].normal);
	}

	i = 0;
	item = NULL;
	cJSON_ArrayForEach(item, json_points)
	{
		// Store in channel map order.
		u_json_get_vec3_array(item, &s[map[i++]].pos);
	}

	// Free the map.
	free(map);
	map = NULL;

	d->lh.sensors = s;
	d->lh.num_sensors = map_size;


	// Transform the sensors into IMU space.
	struct xrt_pose trackref_to_imu = {0};
	math_pose_invert(&d->imu.trackref, &trackref_to_imu);

	for (i = 0; i < d->lh.num_sensors; i++) {
		struct xrt_vec3 point = d->lh.sensors[i].pos;
		struct xrt_vec3 normal = d->lh.sensors[i].normal;

		math_quat_rotate_vec3(&trackref_to_imu.orientation, &normal,
		                      &d->lh.sensors[i].normal);
		math_pose_transform_point(&trackref_to_imu, &point,
		                          &d->lh.sensors[i].pos);
	}
}

static void
_print_vec3(const char *title, struct xrt_vec3 *vec)
{
	printf("%s = %f %f %f\n", title, (double)vec->x, (double)vec->y,
	       (double)vec->z);
}

bool
vive_config_parse(struct vive_device *d, char *json_string)
{
	VIVE_DEBUG(d, "JSON config:\n%s\n", json_string);

	cJSON *json = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json)) {
		VIVE_ERROR("Could not parse JSON data.");
		return false;
	}

	switch (d->variant) {
	case VIVE_VARIANT_VIVE:
		JSON_VEC3(json, "acc_bias", &d->imu.acc_bias);
		JSON_VEC3(json, "acc_scale", &d->imu.acc_scale);
		JSON_VEC3(json, "gyro_bias", &d->imu.gyro_bias);
		JSON_VEC3(json, "gyro_scale", &d->imu.gyro_scale);
		break;
	case VIVE_VARIANT_PRO: {
		const cJSON *imu =
		    cJSON_GetObjectItemCaseSensitive(json, "imu");
		JSON_VEC3(imu, "acc_bias", &d->imu.acc_bias);
		JSON_VEC3(imu, "acc_scale", &d->imu.acc_scale);
		JSON_VEC3(imu, "gyro_bias", &d->imu.gyro_bias);
		JSON_VEC3(imu, "gyro_scale", &d->imu.gyro_scale);
	} break;
	case VIVE_VARIANT_INDEX: {
		const cJSON *head =
		    cJSON_GetObjectItemCaseSensitive(json, "head");
		_get_pose_from_pos_x_z(head, &d->display.trackref);

		const cJSON *imu =
		    cJSON_GetObjectItemCaseSensitive(json, "imu");
		_get_pose_from_pos_x_z(imu, &d->imu.trackref);

		JSON_VEC3(imu, "acc_bias", &d->imu.acc_bias);
		JSON_VEC3(imu, "acc_scale", &d->imu.acc_scale);
		JSON_VEC3(imu, "gyro_bias", &d->imu.gyro_bias);

		_get_lighthouse(d, json);

		struct xrt_pose trackref_to_head;
		struct xrt_pose imu_to_head;

		math_pose_invert(&d->display.trackref, &trackref_to_head);
		math_pose_transform(&trackref_to_head, &d->imu.trackref,
		                    &imu_to_head);

		d->display.imuref = imu_to_head;
	} break;
	default: VIVE_ERROR("Unknown Vive variant.\n"); return false;
	}

	JSON_STRING(json, "model_number", d->firmware.model_number);

	if (d->variant != VIVE_VARIANT_INDEX) {
		JSON_STRING(json, "mb_serial_number",
		            d->firmware.mb_serial_number);
	}
	if (d->variant == VIVE_VARIANT_VIVE) {
		JSON_DOUBLE(json, "lens_separation",
		            &d->display.lens_separation);
	}

	JSON_STRING(json, "device_serial_number",
	            d->firmware.device_serial_number);

	const cJSON *device_json =
	    cJSON_GetObjectItemCaseSensitive(json, "device");
	if (device_json) {
		if (d->variant != VIVE_VARIANT_INDEX) {
			JSON_DOUBLE(device_json, "persistence",
			            &d->display.persistence);
			JSON_FLOAT(
			    device_json, "physical_aspect_x_over_y",
			    &d->base.hmd->distortion.vive.aspect_x_over_y);
		}
		JSON_INT(device_json, "eye_target_height_in_pixels",
		         &d->display.eye_target_height_in_pixels);
		JSON_INT(device_json, "eye_target_width_in_pixels",
		         &d->display.eye_target_width_in_pixels);
	}

	const cJSON *eye_transform_json =
	    cJSON_GetObjectItemCaseSensitive(json, "tracking_to_eye_transform");
	if (eye_transform_json) {
		for (uint8_t eye = 0; eye < 2; eye++) {
			_get_distortion_properties(d, eye_transform_json, eye);
		}
	}

	cJSON_Delete(json);

	// clang-format off
	VIVE_DEBUG(d, "= Vive configuration =");
	VIVE_DEBUG(d, "lens_separation: %f", d->display.lens_separation);
	VIVE_DEBUG(d, "persistence: %f", d->display.persistence);
	VIVE_DEBUG(d, "physical_aspect_x_over_y: %f", (double)d->base.hmd->distortion.vive.aspect_x_over_y);

	VIVE_DEBUG(d, "model_number: %s", d->firmware.model_number);
	VIVE_DEBUG(d, "mb_serial_number: %s", d->firmware.mb_serial_number);
	VIVE_DEBUG(d, "device_serial_number: %s", d->firmware.device_serial_number);

	VIVE_DEBUG(d, "eye_target_height_in_pixels: %d", d->display.eye_target_height_in_pixels);
	VIVE_DEBUG(d, "eye_target_width_in_pixels: %d", d->display.eye_target_width_in_pixels);

	if (d->print_debug) {
		_print_vec3("acc_bias", &d->imu.acc_bias);
		_print_vec3("acc_scale", &d->imu.acc_scale);
		_print_vec3("gyro_bias", &d->imu.gyro_bias);
		_print_vec3("gyro_scale", &d->imu.gyro_scale);
	}

	VIVE_DEBUG(d, "grow_for_undistort: %f", (double)d->base.hmd->distortion.vive.grow_for_undistort);

	VIVE_DEBUG(d, "undistort_r2_cutoff 0: %f", (double)d->base.hmd->distortion.vive.undistort_r2_cutoff[0]);
	VIVE_DEBUG(d, "undistort_r2_cutoff 1: %f", (double)d->base.hmd->distortion.vive.undistort_r2_cutoff[1]);
	// clang-format on

	return true;
}

bool
vive_config_parse_controller(struct vive_controller_device *d,
                             char *json_string)
{
	VIVE_CONTROLLER_DEBUG(d, "JSON config:\n%s\n", json_string);

	cJSON *json = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json)) {
		VIVE_CONTROLLER_ERROR(d, "Could not parse JSON data.");
		return false;
	}

	JSON_STRING(json, "model_number", d->firmware.model_number);
	if (strcmp(d->firmware.model_number, "Vive. Controller MV") == 0) {
		d->variant = CONTROLLER_VIVE_WAND;
		VIVE_CONTROLLER_DEBUG(d, "Found Vive Wand controller");
	} else if (strcmp(d->firmware.model_number, "Knuckles Right") == 0) {
		d->variant = CONTROLLER_INDEX_RIGHT;
		VIVE_CONTROLLER_DEBUG(d, "Found Knuckles Right controller");
	} else if (strcmp(d->firmware.model_number, "Knuckles Left") == 0) {
		d->variant = CONTROLLER_INDEX_LEFT;
		VIVE_CONTROLLER_DEBUG(d, "Found Knuckles Left controller");
	} else {
		VIVE_CONTROLLER_ERROR(d, "Failed to parse controller variant");
	}

	switch (d->variant) {
	case CONTROLLER_VIVE_WAND: {
		JSON_VEC3(json, "acc_bias", &d->imu.acc_bias);
		JSON_VEC3(json, "acc_scale", &d->imu.acc_scale);
		JSON_VEC3(json, "gyro_bias", &d->imu.gyro_bias);
		JSON_VEC3(json, "gyro_scale", &d->imu.gyro_scale);
		JSON_STRING(json, "mb_serial_number",
		            d->firmware.mb_serial_number);
	} break;
	case CONTROLLER_INDEX_LEFT:
	case CONTROLLER_INDEX_RIGHT: {
		const cJSON *imu = u_json_get(json, "imu");
		_get_pose_from_pos_x_z(imu, &d->imu.trackref);

		JSON_VEC3(imu, "acc_bias", &d->imu.acc_bias);
		JSON_VEC3(imu, "acc_scale", &d->imu.acc_scale);
		JSON_VEC3(imu, "gyro_bias", &d->imu.gyro_bias);
	} break;
	default:
		VIVE_CONTROLLER_ERROR(d, "Unknown Vive watchman variant.\n");
		return false;
	}

	JSON_STRING(json, "device_serial_number",
	            d->firmware.device_serial_number);

	cJSON_Delete(json);

	// clang-format off
	VIVE_CONTROLLER_DEBUG(d, "= Vive controller configuration =");

	VIVE_CONTROLLER_DEBUG(d, "model_number: %s", d->firmware.model_number);
	VIVE_CONTROLLER_DEBUG(d, "mb_serial_number: %s", d->firmware.mb_serial_number);
	VIVE_CONTROLLER_DEBUG(d, "device_serial_number: %s", d->firmware.device_serial_number);

	if (d->print_debug) {
		_print_vec3("acc_bias", &d->imu.acc_bias);
		_print_vec3("acc_scale", &d->imu.acc_scale);
		_print_vec3("gyro_bias", &d->imu.gyro_bias);
		_print_vec3("gyro_scale", &d->imu.gyro_scale);
	}

	// clang-format on

	return true;
}
