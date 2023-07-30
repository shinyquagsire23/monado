/* Copyright 2021, Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 */
/*!
 * @file
 * @brief	Driver code to read WMR config blocks
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_wmr
 */

#include "math/m_api.h"

#include "util/u_debug.h"
#include "util/u_misc.h"
#include "util/u_json.h"

#include "wmr_config.h"

#include <assert.h>
#include <string.h>


#define WMR_TRACE(log_level, ...) U_LOG_IFL_T(log_level, __VA_ARGS__)
#define WMR_DEBUG(log_level, ...) U_LOG_IFL_D(log_level, __VA_ARGS__)
#define WMR_INFO(log_level, ...) U_LOG_IFL_I(log_level, __VA_ARGS__)
#define WMR_WARN(log_level, ...) U_LOG_IFL_W(log_level, __VA_ARGS__)
#define WMR_ERROR(log_level, ...) U_LOG_IFL_E(log_level, __VA_ARGS__)

#define JSON_INT(a, b, c) u_json_get_int(u_json_get(a, b), c)
#define JSON_FLOAT(a, b, c) u_json_get_float(u_json_get(a, b), c)
#define JSON_DOUBLE(a, b, c) u_json_get_double(u_json_get(a, b), c)
#define JSON_VEC3(a, b, c) u_json_get_vec3_array(u_json_get(a, b), c)
#define JSON_MATRIX_3X3(a, b, c) u_json_get_matrix_3x3(u_json_get(a, b), c)
#define JSON_STRING(a, b, c) u_json_get_string_into_array(u_json_get(a, b), c, sizeof(c))

//! Specifies the maximum number of cameras to use for SLAM tracking
DEBUG_GET_ONCE_NUM_OPTION(wmr_max_slam_cams, "WMR_MAX_SLAM_CAMS", WMR_MAX_CAMERAS)

static void
wmr_hmd_config_init_defaults(struct wmr_hmd_config *c)
{
	memset(c, 0, sizeof(struct wmr_hmd_config));

	// initialize default sensor transforms
	math_pose_identity(&c->eye_params[0].pose);
	math_pose_identity(&c->eye_params[1].pose);

	math_pose_identity(&c->sensors.accel.pose);
	math_pose_identity(&c->sensors.gyro.pose);
	math_pose_identity(&c->sensors.mag.pose);

	math_matrix_3x3_identity(&c->sensors.accel.mix_matrix);
	math_matrix_3x3_identity(&c->sensors.gyro.mix_matrix);
	math_matrix_3x3_identity(&c->sensors.mag.mix_matrix);
}

static struct xrt_pose
pose_from_rt(const struct xrt_matrix_3x3 rotation_rm, const struct xrt_vec3 translation)
{
	struct xrt_matrix_3x3 rotation_cm;
	math_matrix_3x3_transpose(&rotation_rm, &rotation_cm);

	struct xrt_matrix_4x4 mat = {0};
	math_matrix_4x4_isometry_from_rt(&rotation_cm, &translation, &mat);

	struct xrt_pose pose;
	math_pose_from_isometry(&mat, &pose);

	return pose;
}

static bool
wmr_config_parse_display(struct wmr_hmd_config *c, cJSON *display, enum u_logging_level log_level)
{
	cJSON *json_eye = cJSON_GetObjectItem(display, "AssignedEye");
	char *json_eye_name = cJSON_GetStringValue(json_eye);

	if (json_eye_name == NULL) {
		WMR_ERROR(log_level, "Invalid/missing eye assignment block");
		return false;
	}

	struct wmr_distortion_eye_config *eye = NULL;
	if (!strcmp(json_eye_name, "CALIBRATION_DisplayEyeLeft")) {
		eye = &c->eye_params[0];
	} else if (!strcmp(json_eye_name, "CALIBRATION_DisplayEyeRight")) {
		eye = &c->eye_params[1];
	} else {
		WMR_ERROR(log_level, "Unknown AssignedEye \"%s\"", json_eye_name);
		return false;
	}

	/* Extract display panel parameters */
	cJSON *affine = cJSON_GetObjectItem(display, "Affine");
	if (affine == NULL || u_json_get_float_array(affine, eye->affine_xform.v, 9) != 9) {
		WMR_ERROR(log_level, "Missing affine transform for AssignedEye \"%s\"", json_eye_name);
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

	eye->pose = pose_from_rt(rotation, translation);
	eye->translation = translation;
	eye->rotation = rotation;

	/* Parse color distortion channels */
	const char *channel_names[] = {"DistortionRed", "DistortionGreen", "DistortionBlue"};

	for (int channel = 0; channel < 3; ++channel) {
		struct wmr_distortion_3K *distortion3K = &eye->distortion3K[channel];

		cJSON *dist = cJSON_GetObjectItemCaseSensitive(display, channel_names[channel]);
		if (!dist) {
			WMR_ERROR(log_level, "Missing distortion channel info %s", channel_names[channel]);
			return false;
		}

		const char *model_type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(dist, "ModelType"));
		if (model_type == NULL) {
			WMR_ERROR(log_level, "Missing distortion type");
			return false;
		}

		if (!strcmp(model_type, "CALIBRATION_DisplayDistortionModelPolynomial3K")) {
			distortion3K->model = WMR_DISTORTION_MODEL_POLYNOMIAL_3K;
		} else {
			distortion3K->model = WMR_DISTORTION_MODEL_UNKNOWN;
			WMR_ERROR(log_level, "Unknown distortion model %s", model_type);
			return false;
		}

		int param_count;
		double parameters[5];

		if (!JSON_INT(dist, "ModelParameterCount", &param_count)) {
			WMR_ERROR(log_level, "Missing distortion parameters");
			return false;
		}

		cJSON *params_json = cJSON_GetObjectItemCaseSensitive(dist, "ModelParameters");
		if (params_json == NULL ||
		    u_json_get_double_array(params_json, parameters, param_count) != (size_t)param_count) {
			WMR_ERROR(log_level, "Missing distortion parameters");
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
wmr_inertial_sensor_config_parse(struct wmr_inertial_sensor_config *c, cJSON *sensor, enum u_logging_level log_level)
{
	struct xrt_vec3 translation;
	struct xrt_matrix_3x3 rotation;

	cJSON *rt = cJSON_GetObjectItem(sensor, "Rt");
	cJSON *rx = cJSON_GetObjectItem(rt, "Rotation");
	if (rt == NULL || rx == NULL) {
		WMR_WARN(log_level, "Missing Inertial Sensor calibration");
		return false;
	}

	if (!JSON_VEC3(rt, "Translation", &translation) || u_json_get_float_array(rx, rotation.v, 9) != 9) {
		WMR_WARN(log_level, "Invalid Inertial Sensor calibration");
		return false;
	}

	c->pose = pose_from_rt(rotation, translation);
	c->translation = translation;
	c->rotation = rotation;

	/* compute the bias offsets and mix matrix by taking the constant
	 * coefficients from the configuration */
	cJSON *mix_model = cJSON_GetObjectItem(sensor, "MixingMatrixTemperatureModel");
	cJSON *bias_model = cJSON_GetObjectItem(sensor, "BiasTemperatureModel");
	cJSON *bias_var = cJSON_GetObjectItem(sensor, "BiasUncertainty");
	cJSON *noise_std = cJSON_GetObjectItem(sensor, "Noise");

	float mix_model_values[3 * 3 * 4];
	float bias_model_values[12];
	float bias_var_values[3];
	float noise_std_values[3 * 2];

	if (mix_model == NULL || bias_model == NULL || noise_std == NULL || bias_var == NULL) {
		WMR_WARN(log_level, "Missing Inertial Sensor calibration");
		return false;
	}

	if (u_json_get_float_array(mix_model, mix_model_values, 3 * 3 * 4) != 3 * 3 * 4) {
		WMR_WARN(log_level, "Invalid Inertial Sensor calibration (invalid MixingMatrixTemperatureModel)");
		return false;
	}
	for (int i = 0; i < 9; i++) {
		c->mix_matrix.v[i] = mix_model_values[i * 4];
	}

	if (u_json_get_float_array(bias_model, bias_model_values, 12) != 12) {
		WMR_WARN(log_level, "Invalid Inertial Sensor calibration (invalid BiasTemperatureModel)");
		return false;
	}
	c->bias_offsets.x = bias_model_values[0];
	c->bias_offsets.y = bias_model_values[4];
	c->bias_offsets.z = bias_model_values[8];

	if (u_json_get_float_array(bias_var, bias_var_values, 3) != 3) {
		WMR_WARN(log_level, "Invalid Inertial Sensor calibration (invalid BiasUncertainty)");
		return false;
	}
	c->bias_var.x = bias_var_values[0];
	c->bias_var.y = bias_var_values[1];
	c->bias_var.z = bias_var_values[2];

	if (u_json_get_float_array(noise_std, noise_std_values, 3 * 2) != 3 * 2) {
		WMR_WARN(log_level, "Invalid Inertial Sensor calibration (invalid Noise)");
		return false;
	}
	c->noise_std.x = noise_std_values[0];
	c->noise_std.y = noise_std_values[1];
	c->noise_std.z = noise_std_values[2];
	return true;
}

static bool
wmr_inertial_sensors_config_parse(struct wmr_inertial_sensors_config *c, cJSON *sensor, enum u_logging_level log_level)
{
	struct wmr_inertial_sensor_config *target = NULL;

	const char *sensor_type = cJSON_GetStringValue(cJSON_GetObjectItem(sensor, "SensorType"));
	if (sensor_type == NULL) {
		WMR_WARN(log_level, "Missing sensor type");
		return false;
	}

	if (!strcmp(sensor_type, "CALIBRATION_InertialSensorType_Gyro")) {
		target = &c->gyro;
	} else if (!strcmp(sensor_type, "CALIBRATION_InertialSensorType_Accelerometer")) {
		target = &c->accel;
	} else if (!strcmp(sensor_type, "CALIBRATION_InertialSensorType_Magnetometer")) {
		target = &c->mag;
	} else {
		WMR_WARN(log_level, "Unhandled sensor type \"%s\"", sensor_type);
		return false;
	}

	return wmr_inertial_sensor_config_parse(target, sensor, log_level);
}

static bool
wmr_config_parse_camera_config(struct wmr_hmd_config *c, cJSON *camera, enum u_logging_level log_level)
{
	if (c->cam_count == WMR_MAX_CAMERAS) {
		WMR_ERROR(log_level, "Too many camera entries. Enlarge WMR_MAX_CAMERAS");
		return false;
	}

	struct wmr_camera_config *cam_config = c->cams + c->cam_count;

	/* Camera purpose */
	cJSON *json_purpose = cJSON_GetObjectItem(camera, "Purpose");
	char *json_purpose_name = cJSON_GetStringValue(json_purpose);
	if (json_purpose_name == NULL) {
		WMR_ERROR(log_level, "Invalid camera calibration block %d - camera purpose not found", c->cam_count);
		return false;
	}

	if (!strcmp(json_purpose_name, "CALIBRATION_CameraPurposeHeadTracking")) {
		cam_config->purpose = WMR_CAMERA_PURPOSE_HEAD_TRACKING;
	} else if (!strcmp(json_purpose_name, "CALIBRATION_CameraPurposeDisplayObserver")) {
		cam_config->purpose = WMR_CAMERA_PURPOSE_DISPLAY_OBSERVER;
	} else {
		WMR_ERROR(log_level, "Unknown camera purpose: \"%s\" (camera %d)", json_purpose_name, c->cam_count);
		return false;
	}

	cJSON *json_location = cJSON_GetObjectItem(camera, "Location");
	char *json_location_name = cJSON_GetStringValue(json_location);
	if (json_location_name == NULL) {
		WMR_ERROR(log_level, "Invalid camera calibration block %d - location", c->cam_count);
		return false;
	}

	if (!strcmp(json_location_name, "CALIBRATION_CameraLocationHT0")) {
		cam_config->location = WMR_CAMERA_LOCATION_HT0;
	} else if (!strcmp(json_location_name, "CALIBRATION_CameraLocationHT1")) {
		cam_config->location = WMR_CAMERA_LOCATION_HT1;
	} else if (!strcmp(json_location_name, "CALIBRATION_CameraLocationHT2")) {
		cam_config->location = WMR_CAMERA_LOCATION_HT2;
	} else if (!strcmp(json_location_name, "CALIBRATION_CameraLocationHT3")) {
		cam_config->location = WMR_CAMERA_LOCATION_HT3;
	} else if (!strcmp(json_location_name, "CALIBRATION_CameraLocationDO0")) {
		cam_config->location = WMR_CAMERA_LOCATION_DO0;
	} else if (!strcmp(json_location_name, "CALIBRATION_CameraLocationDO1")) {
		cam_config->location = WMR_CAMERA_LOCATION_DO1;
	} else {
		WMR_ERROR(log_level, "Unknown camera location: \"%s\" (camera %d)", json_location_name, c->cam_count);
		return false;
	}

	/* Camera pose */
	struct xrt_vec3 translation;
	struct xrt_matrix_3x3 rotation;

	cJSON *rt = cJSON_GetObjectItem(camera, "Rt");
	cJSON *rx = cJSON_GetObjectItem(rt, "Rotation");
	if (rt == NULL || rx == NULL) {
		WMR_ERROR(log_level, "Invalid camera calibration block %d - pose", c->cam_count);
		return false;
	}

	if (!JSON_VEC3(rt, "Translation", &translation) || u_json_get_float_array(rx, rotation.v, 9) != 9) {
		WMR_ERROR(log_level, "Invalid camera calibration block %d - pose", c->cam_count);
		return false;
	}

	cam_config->pose = pose_from_rt(rotation, translation);
	cam_config->translation = translation;
	cam_config->rotation = rotation;

	if (!JSON_INT(camera, "SensorWidth", &cam_config->roi.extent.w) ||
	    !JSON_INT(camera, "SensorHeight", &cam_config->roi.extent.h)) {
		WMR_ERROR(log_level, "Invalid camera calibration block %d - sensor size", c->cam_count);
		return false;
	}
	cam_config->roi.offset.w = c->tcam_count * cam_config->roi.extent.w; // Assume all tracking cams have same width
	cam_config->roi.offset.h = 1;                                        // Ignore first metadata row

	/* Distortion information */
	cJSON *dist = cJSON_GetObjectItemCaseSensitive(camera, "Intrinsics");
	if (!dist) {
		WMR_ERROR(log_level, "Invalid camera calibration block %d - distortion", c->cam_count);
		return false;
	}

	const char *model_type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(dist, "ModelType"));
	if (model_type == NULL) {
		WMR_ERROR(log_level, "Invalid camera calibration block %d - missing distortion type", c->cam_count);
		return false;
	}

	if (!strcmp(model_type, "CALIBRATION_LensDistortionModelRational6KT")) {
	} else {
		WMR_ERROR(log_level, "Invalid camera calibration block %d - unknown distortion type %s", c->cam_count,
		          model_type);
		return false;
	}

	struct wmr_distortion_6KT *distortion6KT = &cam_config->distortion6KT;

	int param_count;
	if (!JSON_INT(dist, "ModelParameterCount", &param_count)) {
		WMR_ERROR(log_level, "Invalid camera calibration block %d - no ModelParameterCount", c->cam_count);
		return false;
	}

	if (param_count != 15) {
		WMR_ERROR(log_level, "Invalid camera calibration block %d - wrong ModelParameterCount %d", c->cam_count,
		          param_count);
		return false;
	}

	cJSON *params_json = cJSON_GetObjectItemCaseSensitive(dist, "ModelParameters");
	if (params_json == NULL ||
	    u_json_get_float_array(params_json, distortion6KT->v, param_count) != (size_t)param_count) {
		WMR_ERROR(log_level, "Invalid camera calibration block %d - missing distortion parameters",
		          c->cam_count);
		return false;
	}

	if (cam_config->purpose == WMR_CAMERA_PURPOSE_HEAD_TRACKING) {
		c->tcams[c->tcam_count] = cam_config;
		c->tcam_count++;
	}

	c->cam_count++;
	return true;
}

static bool
wmr_config_parse_calibration(struct wmr_hmd_config *c, cJSON *calib_info, enum u_logging_level log_level)
{
	cJSON *item = NULL;

	// calib_info is object with keys "Cameras", "Displays", and "InertialSensors"
	cJSON *displays = cJSON_GetObjectItemCaseSensitive(calib_info, "Displays");
	if (!cJSON_IsArray(displays)) {
		WMR_ERROR(log_level, "Displays: not found or not an Array");
		return false;
	}

	cJSON_ArrayForEach(item, displays)
	{
		if (!wmr_config_parse_display(c, item, log_level)) {
			WMR_ERROR(log_level, "Error parsing Display entry");
			return false;
		}
	}

	cJSON *sensors = cJSON_GetObjectItemCaseSensitive(calib_info, "InertialSensors");
	if (!cJSON_IsArray(sensors)) {
		WMR_ERROR(log_level, "InertialSensors: not found or not an Array");
		return false;
	}

	cJSON_ArrayForEach(item, sensors)
	{
		if (!wmr_inertial_sensors_config_parse(&c->sensors, item, log_level)) {
			WMR_WARN(log_level, "Error parsing InertialSensor entry");
		}
	}

	cJSON *cameras = cJSON_GetObjectItemCaseSensitive(calib_info, "Cameras");
	if (!cJSON_IsArray(cameras)) {
		WMR_ERROR(log_level, "Cameras: not found or not an Array");
		return false;
	}

	cJSON_ArrayForEach(item, cameras)
	{
		if (!wmr_config_parse_camera_config(c, item, log_level))
			return false;
	}
	c->slam_cam_count = MIN(c->tcam_count, (int)debug_get_num_option_wmr_max_slam_cams());

	return true;
}

static bool
wmr_controller_led_config_parse(struct wmr_led_config *l,
                                int index,
                                const cJSON *led_json,
                                enum u_logging_level log_level)
{
	float tmp[3];

	cJSON *pos = cJSON_GetObjectItem(led_json, "Position");
	if (pos == NULL || u_json_get_float_array(pos, tmp, 3) != 3) {
		WMR_ERROR(log_level, "Missing or invalid position for controller LED %d", index);
		return false;
	}
	l->pos.x = tmp[0];
	l->pos.y = tmp[1];
	l->pos.z = tmp[2];

	cJSON *norm = cJSON_GetObjectItem(led_json, "Normal");
	if (norm == NULL || u_json_get_float_array(norm, tmp, 3) != 3) {
		WMR_ERROR(log_level, "Missing or invalid normal for controller LED %d", index);
		return false;
	}
	l->norm.x = tmp[0];
	l->norm.y = tmp[1];
	l->norm.z = tmp[2];

	return true;
}

bool
wmr_hmd_config_parse(struct wmr_hmd_config *c, char *json_string, enum u_logging_level log_level)
{
	wmr_hmd_config_init_defaults(c);

	cJSON *json_root = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json_root)) {
		WMR_ERROR(log_level, "Could not parse JSON data.");
		cJSON_Delete(json_root);
		return false;
	}

	cJSON *calib_info = cJSON_GetObjectItemCaseSensitive(json_root, "CalibrationInformation");
	if (!cJSON_IsObject(calib_info)) {
		WMR_ERROR(log_level, "CalibrationInformation object not found");
		cJSON_Delete(json_root);
		return false;
	}

	bool res = wmr_config_parse_calibration(c, calib_info, log_level);

	cJSON_Delete(json_root);
	return res;
}

static void
wmr_controller_config_init_defaults(struct wmr_controller_config *c)
{
	memset(c, 0, sizeof(struct wmr_controller_config));

	// initialize default sensor transforms
	math_pose_identity(&c->sensors.accel.pose);
	math_pose_identity(&c->sensors.gyro.pose);
	math_pose_identity(&c->sensors.mag.pose);

	math_matrix_3x3_identity(&c->sensors.accel.mix_matrix);
	math_matrix_3x3_identity(&c->sensors.gyro.mix_matrix);
	math_matrix_3x3_identity(&c->sensors.mag.mix_matrix);
}

bool
wmr_controller_config_parse(struct wmr_controller_config *c, char *json_string, enum u_logging_level log_level)
{
	cJSON *item = NULL;

	wmr_controller_config_init_defaults(c);

	cJSON *json_root = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json_root)) {
		WMR_ERROR(log_level, "Could not parse JSON data.");
		cJSON_Delete(json_root);
		return false;
	}

	cJSON *calib_info = cJSON_GetObjectItemCaseSensitive(json_root, "CalibrationInformation");
	if (!cJSON_IsObject(calib_info)) {
		WMR_ERROR(log_level, "CalibrationInformation object not found");
		cJSON_Delete(json_root);
		return false;
	}

	cJSON *sensors = cJSON_GetObjectItemCaseSensitive(calib_info, "InertialSensors");
	if (!cJSON_IsArray(sensors)) {
		WMR_ERROR(log_level, "InertialSensors: not found or not an Array");
		return false;
	}

	cJSON_ArrayForEach(item, sensors)
	{
		if (!wmr_inertial_sensors_config_parse(&c->sensors, item, log_level)) {
			WMR_WARN(log_level, "Error parsing InertialSensor entry");
		}
	}

	cJSON *leds = cJSON_GetObjectItemCaseSensitive(calib_info, "ControllerLeds");
	if (!cJSON_IsArray(leds)) {
		WMR_ERROR(log_level, "ControllerLeds: not found or not an Array");
		return false;
	}

	cJSON_ArrayForEach(item, leds)
	{
		if (c->led_count == WMR_MAX_LEDS) {
			WMR_ERROR(log_level, "Too many ControllerLed entries. Enlarge WMR_MAX_LEDS");
			return false;
		}

		struct wmr_led_config *led_config = c->leds + c->led_count;

		if (!wmr_controller_led_config_parse(led_config, c->led_count, item, log_level)) {
			WMR_WARN(log_level, "Error parsing ControllerLed entry");
			continue;
		}

		c->led_count++;
	}

	cJSON_Delete(json_root);

	return true;
}

/*!
 * Precompute transforms to convert between OpenXR and WMR coordinate systems.
 *
 * OpenXR: X: Right, Y: Up, Z: Backward
 * WMR: X: Right, Y: Down, Z: Forward
 * ┌────────────────────┐
 * │   OXR       WMR    │
 * │                    │
 * │ ▲ y                │
 * │ │         ▲ z      │
 * │ │    x    │    x   │
 * │ ├──────►  ├──────► │
 * │ │         │        │
 * │ ▼ z       │        │
 * │           ▼ y      │
 * └────────────────────┘
 */
void
wmr_config_precompute_transforms(struct wmr_inertial_sensors_config *sensors,
                                 struct wmr_distortion_eye_config *eye_params)
{
	// P_A_B is such that B = P_A_B * A. See conventions.md
	struct xrt_pose P_oxr_wmr = {{.x = 1.0, .y = 0.0, .z = 0.0, .w = 0.0}, XRT_VEC3_ZERO};
	struct xrt_pose P_wmr_oxr = {0};
	struct xrt_pose P_acc_ht0 = sensors->accel.pose;
	struct xrt_pose P_gyr_ht0 = sensors->gyro.pose;
	struct xrt_pose P_ht0_acc = {0};
	struct xrt_pose P_ht0_gyr = {0};
	struct xrt_pose P_me_ht0 = {0}; // "me" == "middle of the eyes"
	struct xrt_pose P_me_acc = {0};
	struct xrt_pose P_me_gyr = {0};
	struct xrt_pose P_ht0_me = {0};
	struct xrt_pose P_acc_me = {0};
	struct xrt_pose P_oxr_ht0_me = {0}; // P_ht0_me in OpenXR coordinates
	struct xrt_pose P_oxr_acc_me = {0}; // P_acc_me in OpenXR coordinates

	// All of the observed headsets have reported a zero translation for its gyro
	assert(m_vec3_equal_exact(P_gyr_ht0.position, (struct xrt_vec3){0, 0, 0}));

	// Initialize transforms

	// All of these are in WMR coordinates.
	math_pose_invert(&P_oxr_wmr, &P_wmr_oxr); // P_wmr_oxr == P_oxr_wmr
	math_pose_invert(&P_acc_ht0, &P_ht0_acc);
	math_pose_invert(&P_gyr_ht0, &P_ht0_gyr);
	if (eye_params)
		math_pose_interpolate(&eye_params[0].pose, &eye_params[1].pose, 0.5, &P_me_ht0);
	else
		math_pose_identity(&P_me_ht0);
	math_pose_transform(&P_me_ht0, &P_ht0_acc, &P_me_acc);
	math_pose_transform(&P_me_ht0, &P_ht0_gyr, &P_me_gyr);
	math_pose_invert(&P_me_ht0, &P_ht0_me);
	math_pose_invert(&P_me_acc, &P_acc_me);

	// Express P_*_me pose in OpenXR coordinates through sandwich products.
	math_pose_transform(&P_acc_me, &P_wmr_oxr, &P_oxr_acc_me);
	math_pose_transform(&P_oxr_wmr, &P_oxr_acc_me, &P_oxr_acc_me);
	math_pose_transform(&P_ht0_me, &P_wmr_oxr, &P_oxr_ht0_me);
	math_pose_transform(&P_oxr_wmr, &P_oxr_ht0_me, &P_oxr_ht0_me);

	// Save transforms
	math_pose_transform(&P_oxr_wmr, &P_me_acc, &sensors->transforms.P_oxr_acc);
	math_pose_transform(&P_oxr_wmr, &P_me_gyr, &sensors->transforms.P_oxr_gyr);
	sensors->transforms.P_ht0_me = P_oxr_ht0_me;
	sensors->transforms.P_imu_me = P_oxr_acc_me; // Assume accel pose is IMU pose
}
