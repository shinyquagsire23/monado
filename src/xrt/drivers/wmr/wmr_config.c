/* Copyright 2021, Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 */
/*!
 * @file
 * @brief	Driver code to read WMR config blocks
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_wmr
 */
#include <string.h>
#include "math/m_api.h"

#include "util/u_misc.h"
#include "util/u_json.h"

#include "wmr_config.h"

#define WMR_TRACE(ll, ...) U_LOG_IFL_T(ll, __VA_ARGS__)
#define WMR_DEBUG(ll, ...) U_LOG_IFL_D(ll, __VA_ARGS__)
#define WMR_INFO(ll, ...) U_LOG_IFL_I(ll, __VA_ARGS__)
#define WMR_WARN(ll, ...) U_LOG_IFL_W(ll, __VA_ARGS__)
#define WMR_ERROR(ll, ...) U_LOG_IFL_E(ll, __VA_ARGS__)

#define JSON_INT(a, b, c) u_json_get_int(u_json_get(a, b), c)
#define JSON_FLOAT(a, b, c) u_json_get_float(u_json_get(a, b), c)
#define JSON_DOUBLE(a, b, c) u_json_get_double(u_json_get(a, b), c)
#define JSON_VEC3(a, b, c) u_json_get_vec3_array(u_json_get(a, b), c)
#define JSON_MATRIX_3X3(a, b, c) u_json_get_matrix_3x3(u_json_get(a, b), c)
#define JSON_STRING(a, b, c) u_json_get_string_into_array(u_json_get(a, b), c, sizeof(c))

static void
wmr_config_init_defaults(struct wmr_hmd_config *c)
{
	memset(c, 0, sizeof(struct wmr_hmd_config));

	// initialize default sensor transforms
	math_pose_identity(&c->eye_params[0].pose);
	math_pose_identity(&c->eye_params[1].pose);
	math_pose_identity(&c->accel_pose);
	math_pose_identity(&c->gyro_pose);
	math_pose_identity(&c->mag_pose);
}

static void
wmr_config_compute_pose(struct xrt_pose *out_pose, const struct xrt_vec3 *tx, const struct xrt_matrix_3x3 *rx)
{
	// Adjust the coordinate system / conventions of the raw Tx and Rx config to yield a usable xrt_pose
	// The config stores a 3x3 rotation matrix and a vec3 translation.
	// Translation is applied after rotation, and the coordinate system is flipped in YZ.

	struct xrt_matrix_3x3 coordsys = {.v = {1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, -1.0}};

	struct xrt_matrix_3x3 rx_adj;
	math_matrix_3x3_multiply(&coordsys, rx, &rx_adj);
	math_quat_from_matrix_3x3(&rx_adj, &out_pose->orientation);

	struct xrt_vec3 v;
	math_matrix_3x3_transform_vec3(&coordsys, tx, &v);
	math_matrix_3x3_transform_vec3(&rx_adj, &v, &out_pose->position);
}

static bool
wmr_config_parse_display(struct wmr_hmd_config *c, cJSON *display, enum u_logging_level ll)
{
	cJSON *json_eye = cJSON_GetObjectItem(display, "AssignedEye");
	char *json_eye_name = cJSON_GetStringValue(json_eye);

	if (json_eye_name == NULL) {
		WMR_ERROR(ll, "Invalid/missing eye assignment block");
		return false;
	}

	struct wmr_distortion_eye_config *eye = NULL;
	if (!strcmp(json_eye_name, "CALIBRATION_DisplayEyeLeft")) {
		eye = &c->eye_params[0];
	} else if (!strcmp(json_eye_name, "CALIBRATION_DisplayEyeRight")) {
		eye = &c->eye_params[1];
	} else {
		WMR_ERROR(ll, "Unknown AssignedEye \"%s\"", json_eye_name);
		return false;
	}

	/* Extract display panel parameters */
	cJSON *affine = cJSON_GetObjectItem(display, "Affine");
	if (affine == NULL || u_json_get_float_array(affine, eye->affine_xform.v, 9) != 9) {
		WMR_ERROR(ll, "Missing affine transform for AssignedEye \"%s\"", json_eye_name);
		return false;
	}

	if (!JSON_FLOAT(display, "DisplayWidth", &eye->display_size.x) ||
	    !JSON_FLOAT(display, "DisplayHeight", &eye->display_size.y))
		return false;

	cJSON *visible_area_center = cJSON_GetObjectItem(display, "VisibleAreaCenter");
	if (visible_area_center == NULL || !JSON_FLOAT(visible_area_center, "X", &eye->visible_center.x) ||
	    !JSON_FLOAT(visible_area_center, "Y", &eye->visible_center.y)) {
		return false;
	}

	if (!JSON_DOUBLE(display, "VisibleAreaRadius", &eye->visible_radius))
		return false;

	/* Compute eye pose */
	cJSON *rt = cJSON_GetObjectItem(display, "Rt");
	cJSON *rx = cJSON_GetObjectItem(rt, "Rotation");
	if (rt == NULL || rx == NULL)
		return false;

	struct xrt_vec3 translation;
	struct xrt_matrix_3x3 rotation;

	if (!JSON_VEC3(rt, "Translation", &translation))
		return false;

	if (u_json_get_float_array(rx, rotation.v, 9) != 9)
		return false;

	wmr_config_compute_pose(&eye->pose, &translation, &rotation);

	/* Parse color distortion channels */
	const char *channel_names[] = {"DistortionRed", "DistortionGreen", "DistortionBlue"};

	for (int channel = 0; channel < 3; ++channel) {
		struct wmr_distortion_3K *distortion3K = &eye->distortion3K[channel];

		cJSON *dist = cJSON_GetObjectItemCaseSensitive(display, channel_names[channel]);
		if (!dist) {
			WMR_ERROR(ll, "Missing distortion channel info %s", channel_names[channel]);
			return false;
		}

		const char *model_type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(dist, "ModelType"));
		if (model_type == NULL) {
			WMR_ERROR(ll, "Missing distortion type");
			return false;
		}

		if (!strcmp(model_type, "CALIBRATION_DisplayDistortionModelPolynomial3K")) {
			distortion3K->model = WMR_DISTORTION_MODEL_POLYNOMIAL_3K;
		} else {
			distortion3K->model = WMR_DISTORTION_MODEL_UNKNOWN;
			WMR_ERROR(ll, "Unknown distortion model %s", model_type);
			return false;
		}

		int param_count;
		double parameters[5];

		if (!JSON_INT(dist, "ModelParameterCount", &param_count)) {
			WMR_ERROR(ll, "Missing distortion parameters");
			return false;
		}

		cJSON *params_json = cJSON_GetObjectItemCaseSensitive(dist, "ModelParameters");
		if (params_json == NULL ||
		    u_json_get_double_array(params_json, parameters, param_count) != (size_t)param_count) {
			WMR_ERROR(ll, "Missing distortion parameters");
			return false;
		}

		distortion3K->eye_center.x = parameters[0];
		distortion3K->eye_center.y = parameters[1];

		distortion3K->k[0] = parameters[2];
		distortion3K->k[1] = parameters[3];
		distortion3K->k[2] = parameters[4];
	}

	return true;
}

static bool
wmr_config_parse_inertial_sensor(struct wmr_hmd_config *c, cJSON *sensor, enum u_logging_level ll)
{
	struct xrt_pose *out_pose;

	const char *sensor_type = cJSON_GetStringValue(cJSON_GetObjectItem(sensor, "SensorType"));
	if (sensor_type == NULL) {
		WMR_WARN(ll, "Missing sensor type");
		return false;
	}

	if (!strcmp(sensor_type, "CALIBRATION_InertialSensorType_Gyro")) {
		out_pose = &c->gyro_pose;
	} else if (!strcmp(sensor_type, "CALIBRATION_InertialSensorType_Accelerometer")) {
		out_pose = &c->accel_pose;
	} else if (!strcmp(sensor_type, "CALIBRATION_InertialSensorType_Magnetometer")) {
		out_pose = &c->mag_pose;
	} else {
		WMR_WARN(ll, "Unhandled sensor type \"%s\"", sensor_type);
		return false;
	}

	struct xrt_vec3 translation;
	struct xrt_matrix_3x3 rotation;

	cJSON *rt = cJSON_GetObjectItem(sensor, "Rt");
	cJSON *rx = cJSON_GetObjectItem(rt, "Rotation");
	if (rt == NULL || rx == NULL) {
		WMR_WARN(ll, "Missing Inertial Sensor calibration");
		return false;
	}

	if (!JSON_VEC3(rt, "Translation", &translation) || u_json_get_float_array(rx, rotation.v, 9) != 9) {
		WMR_WARN(ll, "Invalid Inertial Sensor calibration");
		return false;
	}

	wmr_config_compute_pose(out_pose, &translation, &rotation);

	return true;
}


static bool
wmr_config_parse_calibration(struct wmr_hmd_config *c, cJSON *calib_info, enum u_logging_level ll)
{
	cJSON *item = NULL;

	// calib_info is object with keys "Cameras", "Displays", and "InertialSensors"
	cJSON *displays = cJSON_GetObjectItemCaseSensitive(calib_info, "Displays");
	if (!cJSON_IsArray(displays)) {
		WMR_ERROR(ll, "Displays: not found or not an Array");
		return false;
	}

	cJSON_ArrayForEach(item, displays)
	{
		if (!wmr_config_parse_display(c, item, ll)) {
			WMR_ERROR(ll, "Error parsing Display entry");
			return false;
		}
	}

	cJSON *sensors = cJSON_GetObjectItemCaseSensitive(calib_info, "InertialSensors");
	if (!cJSON_IsArray(sensors)) {
		WMR_ERROR(ll, "InertialSensors: not found or not an Array");
		return false;
	}

	cJSON_ArrayForEach(item, sensors)
	{
		if (!wmr_config_parse_inertial_sensor(c, item, ll)) {
			WMR_WARN(ll, "Error parsing InertialSensor entry");
		}
	}

	return true;
}


bool
wmr_config_parse(struct wmr_hmd_config *c, char *json_string, enum u_logging_level ll)
{
	wmr_config_init_defaults(c);

	cJSON *json_root = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json_root)) {
		WMR_ERROR(ll, "Could not parse JSON data.");
		cJSON_Delete(json_root);
		return false;
	}

	cJSON *calib_info = cJSON_GetObjectItemCaseSensitive(json_root, "CalibrationInformation");
	if (!cJSON_IsObject(calib_info)) {
		WMR_ERROR(ll, "CalibrationInformation object not found");
		cJSON_Delete(json_root);
		return false;
	}

	bool res = wmr_config_parse_calibration(c, calib_info, ll);

	cJSON_Delete(json_root);
	return res;
}
