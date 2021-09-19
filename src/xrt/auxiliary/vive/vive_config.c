// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vive json implementation
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_vive
 */

#include <stdio.h>

#include "vive_config.h"

#include "util/u_misc.h"
#include "util/u_json.h"
#include "util/u_distortion_mesh.h"

#include "math/m_api.h"

#include "tracking/t_tracking.h"
#include "math/m_vec3.h"
#include "math/m_space.h"


#define VIVE_TRACE(d, ...) U_LOG_IFL_T(d->ll, __VA_ARGS__)
#define VIVE_DEBUG(d, ...) U_LOG_IFL_D(d->ll, __VA_ARGS__)
#define VIVE_INFO(d, ...) U_LOG_IFL_I(d->ll, __VA_ARGS__)
#define VIVE_WARN(d, ...) U_LOG_IFL_W(d->ll, __VA_ARGS__)
#define VIVE_ERROR(d, ...) U_LOG_IFL_E(d->ll, __VA_ARGS__)

#define JSON_INT(a, b, c) u_json_get_int(u_json_get(a, b), c)
#define JSON_FLOAT(a, b, c) u_json_get_float(u_json_get(a, b), c)
#define JSON_DOUBLE(a, b, c) u_json_get_double(u_json_get(a, b), c)
#define JSON_VEC3(a, b, c) u_json_get_vec3_array(u_json_get(a, b), c)
#define JSON_MATRIX_3X3(a, b, c) u_json_get_matrix_3x3(u_json_get(a, b), c)
#define JSON_STRING(a, b, c) u_json_get_string_into_array(u_json_get(a, b), c, sizeof(c))

#define printf_pose(pose)                                                                                              \
	printf("%f %f %f  %f %f %f %f\n", pose.position.x, pose.position.y, pose.position.z, pose.orientation.x,       \
	       pose.orientation.y, pose.orientation.z, pose.orientation.w);

static void
_get_color_coeffs(struct u_vive_values *values, const cJSON *coeffs, uint8_t eye, uint8_t channel)
{
	// For Vive this is 8 with only 3 populated.
	// For Index this is 4 with all values populated.
	const cJSON *item = NULL;
	size_t i = 0;
	cJSON_ArrayForEach(item, coeffs)
	{
		values->coefficients[channel][i] = (float)item->valuedouble;
		++i;
		if (i == 4) {
			break;
		}
	}
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
_get_distortion_properties(struct vive_config *d, const cJSON *eye_transform_json, uint8_t eye)
{
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
	JSON_FLOAT(eye_json, "grow_for_undistort", &d->distortion[eye].grow_for_undistort);
	JSON_FLOAT(eye_json, "undistort_r2_cutoff", &d->distortion[eye].undistort_r2_cutoff);
	// clang-format on

	const char *names[3] = {
	    "distortion_red",
	    "distortion",
	    "distortion_blue",
	};

	for (int i = 0; i < 3; i++) {
		const cJSON *distortion = cJSON_GetObjectItemCaseSensitive(eye_json, names[i]);
		if (distortion == NULL) {
			continue;
		}

		JSON_FLOAT(distortion, "center_x", &d->distortion[eye].center[i].x);
		JSON_FLOAT(distortion, "center_y", &d->distortion[eye].center[i].y);

		const cJSON *coeffs = cJSON_GetObjectItemCaseSensitive(distortion, "coeffs");
		if (coeffs != NULL) {
			_get_color_coeffs(&d->distortion[eye], coeffs, eye, i);
		}
	}
}

static void
_get_lighthouse(struct vive_config *d, const cJSON *json)
{
	const cJSON *lh = cJSON_GetObjectItemCaseSensitive(json, "lighthouse_config");
	if (lh == NULL) {
		return;
	}

	const cJSON *json_map = cJSON_GetObjectItemCaseSensitive(lh, "channelMap");
	const cJSON *json_normals = cJSON_GetObjectItemCaseSensitive(lh, "modelNormals");
	const cJSON *json_points = cJSON_GetObjectItemCaseSensitive(lh, "modelPoints");

	if (json_map == NULL || json_normals == NULL || json_points == NULL) {
		return;
	}

	size_t map_size = cJSON_GetArraySize(json_map);
	size_t normals_size = cJSON_GetArraySize(json_normals);
	size_t points_size = cJSON_GetArraySize(json_points);

	if (map_size != normals_size || normals_size != points_size || map_size <= 0) {
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
	struct xrt_pose trackref_to_imu = XRT_POSE_IDENTITY;
	math_pose_invert(&d->imu.trackref, &trackref_to_imu);

	for (i = 0; i < d->lh.num_sensors; i++) {
		struct xrt_vec3 point = d->lh.sensors[i].pos;
		struct xrt_vec3 normal = d->lh.sensors[i].normal;

		math_quat_rotate_vec3(&trackref_to_imu.orientation, &normal, &d->lh.sensors[i].normal);
		math_pose_transform_point(&trackref_to_imu, &point, &d->lh.sensors[i].pos);
	}
}

static void
_print_vec3(const char *title, struct xrt_vec3 *vec)
{
	U_LOG_D("%s = %f %f %f", title, (double)vec->x, (double)vec->y, (double)vec->z);
}

static bool
_get_camera(struct index_camera *cam, const cJSON *cam_json)
{
	bool succeeded = true;
	const cJSON *extrinsics = u_json_get(cam_json, "extrinsics");
	succeeded = succeeded && JSON_VEC3(extrinsics, "plus_x", &cam->extrinsics.plus_x);
	succeeded = succeeded && JSON_VEC3(extrinsics, "plus_z", &cam->extrinsics.plus_z);
	succeeded = succeeded && JSON_VEC3(extrinsics, "position", &cam->extrinsics.position);
	_get_pose_from_pos_x_z(extrinsics, &cam->trackref);


	const cJSON *intrinsics = u_json_get(cam_json, "intrinsics");

	succeeded = succeeded && u_json_get_double_array(u_json_get(u_json_get(intrinsics, "distort"), "coeffs"),
	                                                 cam->intrinsics.distortion, 4);

	succeeded = succeeded && u_json_get_double(u_json_get(intrinsics, "center_x"), &cam->intrinsics.center_x);
	succeeded = succeeded && u_json_get_double(u_json_get(intrinsics, "center_y"), &cam->intrinsics.center_y);

	succeeded = succeeded && u_json_get_double(u_json_get(intrinsics, "focal_x"), &cam->intrinsics.focal_x);
	succeeded = succeeded && u_json_get_double(u_json_get(intrinsics, "focal_y"), &cam->intrinsics.focal_y);
	succeeded = succeeded && u_json_get_int(u_json_get(intrinsics, "height"), &cam->intrinsics.image_size_pixels.h);
	succeeded = succeeded && u_json_get_int(u_json_get(intrinsics, "width"), &cam->intrinsics.image_size_pixels.w);

	if (!succeeded) {
		return false;
	}
	return true;
}

static bool
_get_cameras(struct vive_config *d, const cJSON *cameras_json)
{
	const cJSON *cmr = NULL;

	bool found_camera_json = false;
	bool succeeded_parsing_json = false;

	cJSON_ArrayForEach(cmr, cameras_json)
	{
		found_camera_json = true;

		const cJSON *name_json = u_json_get(cmr, "name");
		const char *name = name_json->valuestring;
		bool is_left = !strcmp("left", name);
		bool is_right = !strcmp("right", name);

		if (!is_left && !is_right) {
			continue;
		}

		if (!_get_camera(&d->cameras.view[is_right], cmr)) {
			succeeded_parsing_json = false;
			break;
		}

		succeeded_parsing_json = true;
	}

	if (!found_camera_json) {
		U_LOG_W("HMD is Index, but no cameras in json file!");
		return false;
	} else if (!succeeded_parsing_json) {
		U_LOG_E("Failed to parse Index camera calibration!");
		return false;
	}

	struct xrt_pose trackref_to_head;
	struct xrt_pose camera_to_head;
	math_pose_invert(&d->display.trackref, &trackref_to_head);

	math_pose_transform(&trackref_to_head, &d->cameras.view[0].trackref, &camera_to_head);
	d->cameras.view[0].headref = camera_to_head;

	math_pose_transform(&trackref_to_head, &d->cameras.view[1].trackref, &camera_to_head);
	d->cameras.view[1].headref = camera_to_head;

	// Calculate where in the right camera space the left camera is.
	struct xrt_pose invert, left_in_right;
	math_pose_invert(&d->cameras.view[1].headref, &invert);
	math_pose_transform(&d->cameras.view[0].headref, &invert, &left_in_right);
	d->cameras.left_in_right = left_in_right;

	// To turn it into OpenCV cameras coordinate system.
	struct xrt_pose opencv = left_in_right;
	opencv.orientation.x = -left_in_right.orientation.x;
	opencv.position.y = -left_in_right.position.y;
	opencv.position.z = -left_in_right.position.z;
	d->cameras.opencv = opencv;

	d->cameras.valid = true;

	return true;
}

bool
vive_get_stereo_camera_calibration(struct vive_config *d,
                                   struct t_stereo_camera_calibration **out_calibration,
                                   struct xrt_pose *head_in_left_camera)
{
	// Note! This doesn't feel exactly correct - some parts of the math seem weird, and when undistorting the
	// images with the data produced by this function, things don't quite line up along epipolar lines. If you're
	// really good at this kind of math and very bored, you could try to figure out what the problem is here. I for
	// one have to accept that I don't have time for this and have to merge it as-is.
	if (!d->cameras.valid) {
		U_LOG_W("Why did you call me?");
		return false;
	}
	struct index_camera *cameras = d->cameras.view;
	bool print_debug = false;
	struct t_stereo_camera_calibration *out_calib = NULL;

	t_stereo_camera_calibration_alloc(&out_calib, 5);

	*out_calibration = out_calib;

	struct xrt_matrix_3x3 rotate_hmd_to_camera[2];
	struct xrt_matrix_3x3 rotate_camera_to_hmd[2];

	for (int i = 0; i < 2; i++) {
		out_calib->view[i].use_fisheye = true;
		out_calib->view[i].image_size_pixels.h = 960;
		out_calib->view[i].image_size_pixels.w = 960;

		// This better be row-major!
		out_calib->view[i].intrinsics[0][0] = cameras[i].intrinsics.focal_x;
		out_calib->view[i].intrinsics[0][1] = 0.0f;
		out_calib->view[i].intrinsics[0][2] = cameras[i].intrinsics.center_x;

		out_calib->view[i].intrinsics[1][0] = 0.0f;
		out_calib->view[i].intrinsics[1][1] = cameras[i].intrinsics.focal_y;
		out_calib->view[i].intrinsics[1][2] = cameras[i].intrinsics.center_y;

		out_calib->view[i].intrinsics[2][0] = 0.0f;
		out_calib->view[i].intrinsics[2][1] = 0.0f;
		out_calib->view[i].intrinsics[2][2] = 1.0f;


		out_calib->view[i].distortion_fisheye[0] = cameras[i].intrinsics.distortion[0];
		out_calib->view[i].distortion_fisheye[1] = cameras[i].intrinsics.distortion[1];
		out_calib->view[i].distortion_fisheye[2] = cameras[i].intrinsics.distortion[2];
		out_calib->view[i].distortion_fisheye[3] = cameras[i].intrinsics.distortion[3];


		struct xrt_vec3 plus_x = m_vec3_mul_scalar(cameras[i].extrinsics.plus_x, -1.0f);
		struct xrt_vec3 plus_z = m_vec3_mul_scalar(cameras[i].extrinsics.plus_z, -1.0f);
		struct xrt_vec3 plus_y;
		math_vec3_cross(&plus_z, &plus_x, &plus_y);

		// I will have a seziure if you ask me if this is row-major or col-major. It works, okay? *OKAY?*
		rotate_hmd_to_camera[i].v[0] = plus_x.x;
		rotate_hmd_to_camera[i].v[1] = plus_y.x;
		rotate_hmd_to_camera[i].v[2] = plus_z.x;

		rotate_hmd_to_camera[i].v[3] = plus_x.y;
		rotate_hmd_to_camera[i].v[4] = plus_y.y;
		rotate_hmd_to_camera[i].v[5] = plus_z.y;

		rotate_hmd_to_camera[i].v[6] = plus_x.z;
		rotate_hmd_to_camera[i].v[7] = plus_y.z;
		rotate_hmd_to_camera[i].v[8] = plus_z.z;

		math_matrix_3x3_inverse(&rotate_hmd_to_camera[i], &rotate_camera_to_hmd[i]);
	}

	// The Index has hard-coded position intrinsics: relative to the HMD origin, both cameras have the exact same Y
	// and Z coordinate, and their X-coordinates have the same magnitude but opposite signs. In other words their
	// positions are mirrored across the YZ plane. So, I can do cheesy shortcuts like this.
	float negative_baseline = cameras[1].extrinsics.position.x - cameras[0].extrinsics.position.x;

	// Doing rotate_camera_to_hmd * (baseline, 0, 0)
	out_calib->camera_translation[0] = negative_baseline * rotate_camera_to_hmd[1].v[0];
	out_calib->camera_translation[1] = negative_baseline * rotate_camera_to_hmd[1].v[1];
	out_calib->camera_translation[2] = negative_baseline * rotate_camera_to_hmd[1].v[2];


	struct xrt_matrix_3x3 rot_left_camera_to_right_camera;

	math_matrix_3x3_multiply(&rotate_hmd_to_camera[1], &rotate_camera_to_hmd[0], &rot_left_camera_to_right_camera);

	out_calib->camera_rotation[0][0] = rot_left_camera_to_right_camera.v[0];
	out_calib->camera_rotation[0][1] = rot_left_camera_to_right_camera.v[1];
	out_calib->camera_rotation[0][2] = rot_left_camera_to_right_camera.v[2];

	out_calib->camera_rotation[1][0] = rot_left_camera_to_right_camera.v[3];
	out_calib->camera_rotation[1][1] = rot_left_camera_to_right_camera.v[4];
	out_calib->camera_rotation[1][2] = rot_left_camera_to_right_camera.v[5];

	out_calib->camera_rotation[2][0] = rot_left_camera_to_right_camera.v[6];
	out_calib->camera_rotation[2][1] = rot_left_camera_to_right_camera.v[7];
	out_calib->camera_rotation[2][2] = rot_left_camera_to_right_camera.v[8];


	struct xrt_space_graph xsg_hmd_in_left_cam = {0};

	// For some reason xrt_space_graph wants things in the opposite order as you'd expect.
	// Second, we apply the position:
	struct xrt_pose just_translation = {0};
	just_translation.orientation.w = 1.0f;
	just_translation.orientation.x = 0.0f;
	just_translation.orientation.y = 0.0f;
	just_translation.orientation.z = 0.0f;

	// Weird that only the y-component has to be negative, right? I, the person writing this, don't really
	// understand it. The Index calibration sure uses weird coordinate spaces.
	just_translation.position.x = cameras[0].extrinsics.position.x;
	just_translation.position.y = -cameras[0].extrinsics.position.y;
	just_translation.position.z = cameras[0].extrinsics.position.z;

	m_space_graph_add_pose(&xsg_hmd_in_left_cam, &just_translation);

	// First, we add the rotation:
	struct xrt_pose just_rotation = {0};
	math_quat_from_matrix_3x3(&rotate_camera_to_hmd[0], &just_rotation.orientation);

	m_space_graph_add_pose(&xsg_hmd_in_left_cam, &just_rotation);

	struct xrt_space_relation head_in_left_cam = {0};

	m_space_graph_resolve(&xsg_hmd_in_left_cam, &head_in_left_cam);

	if (print_debug) {
		printf_pose(head_in_left_cam.pose);
	}

	*head_in_left_camera = head_in_left_cam.pose;
	return true;
}

static void
vive_init_defaults(struct vive_config *d)
{
	d->display.eye_target_width_in_pixels = 1080;
	d->display.eye_target_height_in_pixels = 1200;

	d->display.rot[0].w = 1.0f;
	d->display.rot[1].w = 1.0f;

	d->imu.gyro_range = 8.726646f;
	d->imu.acc_range = 39.226600f;

	d->imu.acc_scale.x = 1.0f;
	d->imu.acc_scale.y = 1.0f;
	d->imu.acc_scale.z = 1.0f;

	d->imu.gyro_scale.x = 1.0f;
	d->imu.gyro_scale.y = 1.0f;
	d->imu.gyro_scale.z = 1.0f;

	d->cameras.valid = false;

	for (int view = 0; view < 2; view++) {
		d->distortion[view].aspect_x_over_y = 0.89999997615814209f;
		d->distortion[view].grow_for_undistort = 0.5f;
		d->distortion[view].undistort_r2_cutoff = 1.0f;
	}
}

bool
vive_config_parse(struct vive_config *d, char *json_string, enum u_logging_level ll)
{
	d->ll = ll;
	vive_init_defaults(d);

	VIVE_DEBUG(d, "JSON config:\n%s", json_string);

	cJSON *json = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json)) {
		VIVE_ERROR(d, "Could not parse JSON data.");
		vive_config_teardown(d);
		return false;
	}

	if (u_json_get(json, "model_number")) {
		JSON_STRING(json, "model_number", d->firmware.model_number);
	} else {
		JSON_STRING(json, "model_name", d->firmware.model_number);
	}

	VIVE_DEBUG(d, "Parsing model number: %s", d->firmware.model_number);

	if (strcmp(d->firmware.model_number, "Utah MP") == 0) {
		d->variant = VIVE_VARIANT_INDEX;
		VIVE_DEBUG(d, "Found Valve Index HMD");
	} else if (strcmp(d->firmware.model_number, "Vive MV") == 0 ||
	           strcmp(d->firmware.model_number, "Vive MV.") == 0 ||
	           strcmp(d->firmware.model_number, "Vive. MV") == 0) {
		d->variant = VIVE_VARIANT_VIVE;
		VIVE_DEBUG(d, "Found HTC Vive HMD");
	} else if (strcmp(d->firmware.model_number, "Vive_Pro MV") == 0) {
		d->variant = VIVE_VARIANT_PRO;
		VIVE_DEBUG(d, "Found HTC Vive Pro HMD");
	} else {
		VIVE_ERROR(d, "Failed to parse Vive HMD variant");
	}

	switch (d->variant) {
	case VIVE_VARIANT_VIVE:
		JSON_VEC3(json, "acc_bias", &d->imu.acc_bias);
		JSON_VEC3(json, "acc_scale", &d->imu.acc_scale);
		JSON_VEC3(json, "gyro_bias", &d->imu.gyro_bias);
		JSON_VEC3(json, "gyro_scale", &d->imu.gyro_scale);
		break;
	case VIVE_VARIANT_PRO: {
		const cJSON *imu = cJSON_GetObjectItemCaseSensitive(json, "imu");
		JSON_VEC3(imu, "acc_bias", &d->imu.acc_bias);
		JSON_VEC3(imu, "acc_scale", &d->imu.acc_scale);
		JSON_VEC3(imu, "gyro_bias", &d->imu.gyro_bias);
		JSON_VEC3(imu, "gyro_scale", &d->imu.gyro_scale);
	} break;
	case VIVE_VARIANT_INDEX: {
		const cJSON *head = cJSON_GetObjectItemCaseSensitive(json, "head");
		_get_pose_from_pos_x_z(head, &d->display.trackref);

		const cJSON *imu = cJSON_GetObjectItemCaseSensitive(json, "imu");
		_get_pose_from_pos_x_z(imu, &d->imu.trackref);

		JSON_VEC3(imu, "acc_bias", &d->imu.acc_bias);
		JSON_VEC3(imu, "acc_scale", &d->imu.acc_scale);
		JSON_VEC3(imu, "gyro_bias", &d->imu.gyro_bias);

		_get_lighthouse(d, json);

		struct xrt_pose trackref_to_head;
		struct xrt_pose imu_to_head;

		math_pose_invert(&d->display.trackref, &trackref_to_head);
		math_pose_transform(&trackref_to_head, &d->imu.trackref, &imu_to_head);

		d->display.imuref = imu_to_head;

		const cJSON *cameras_json = u_json_get(json, "tracked_cameras");
		_get_cameras(d, cameras_json);
	} break;
	default:
		VIVE_ERROR(d, "Unknown Vive variant.");
		vive_config_teardown(d);
		return false;
	}

	if (d->variant != VIVE_VARIANT_INDEX) {
		JSON_STRING(json, "mb_serial_number", d->firmware.mb_serial_number);
	}
	if (d->variant == VIVE_VARIANT_VIVE) {
		JSON_DOUBLE(json, "lens_separation", &d->display.lens_separation);
	}

	JSON_STRING(json, "device_serial_number", d->firmware.device_serial_number);

	const cJSON *device_json = cJSON_GetObjectItemCaseSensitive(json, "device");
	if (device_json) {
		if (d->variant != VIVE_VARIANT_INDEX) {
			JSON_DOUBLE(device_json, "persistence", &d->display.persistence);
			JSON_FLOAT(device_json, "physical_aspect_x_over_y", &d->distortion[0].aspect_x_over_y);

			d->distortion[1].aspect_x_over_y = d->distortion[0].aspect_x_over_y;
		}
		JSON_INT(device_json, "eye_target_height_in_pixels", &d->display.eye_target_height_in_pixels);
		JSON_INT(device_json, "eye_target_width_in_pixels", &d->display.eye_target_width_in_pixels);
	}

	const cJSON *eye_transform_json = cJSON_GetObjectItemCaseSensitive(json, "tracking_to_eye_transform");
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
	VIVE_DEBUG(d, "physical_aspect_x_over_y: %f", (double)d->distortion[0].aspect_x_over_y);

	VIVE_DEBUG(d, "model_number: %s", d->firmware.model_number);
	VIVE_DEBUG(d, "mb_serial_number: %s", d->firmware.mb_serial_number);
	VIVE_DEBUG(d, "device_serial_number: %s", d->firmware.device_serial_number);

	VIVE_DEBUG(d, "eye_target_height_in_pixels: %d", d->display.eye_target_height_in_pixels);
	VIVE_DEBUG(d, "eye_target_width_in_pixels: %d", d->display.eye_target_width_in_pixels);

	if (d->ll <= U_LOGGING_DEBUG) {
		_print_vec3("acc_bias", &d->imu.acc_bias);
		_print_vec3("acc_scale", &d->imu.acc_scale);
		_print_vec3("gyro_bias", &d->imu.gyro_bias);
		_print_vec3("gyro_scale", &d->imu.gyro_scale);
	}

	VIVE_DEBUG(d, "grow_for_undistort: %f", (double)d->distortion[0].grow_for_undistort);

	VIVE_DEBUG(d, "undistort_r2_cutoff 0: %f", (double)d->distortion[0].undistort_r2_cutoff);
	VIVE_DEBUG(d, "undistort_r2_cutoff 1: %f", (double)d->distortion[1].undistort_r2_cutoff);
	// clang-format on

	return true;
}

void
vive_config_teardown(struct vive_config *config)
{
	if (config->lh.sensors != NULL) {
		free(config->lh.sensors);
		config->lh.sensors = NULL;
		config->lh.num_sensors = 0;
	}
}

bool
vive_config_parse_controller(struct vive_controller_config *d, char *json_string, enum u_logging_level ll)
{
	d->ll = ll;
	VIVE_DEBUG(d, "JSON config:\n%s", json_string);

	cJSON *json = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json)) {
		VIVE_ERROR(d, "Could not parse JSON data.");
		return false;
	}


	if (u_json_get(json, "model_number")) {
		JSON_STRING(json, "model_number", d->firmware.model_number);
	} else {
		JSON_STRING(json, "model_name", d->firmware.model_number);
	}

	VIVE_DEBUG(d, "Parsing model number: %s", d->firmware.model_number);

	if (strcmp(d->firmware.model_number, "Vive. Controller MV") == 0 ||
	    strcmp(d->firmware.model_number, "Vive Controller MV") == 0) {
		d->variant = CONTROLLER_VIVE_WAND;
		VIVE_DEBUG(d, "Found Vive Wand controller");
	} else if (strcmp(d->firmware.model_number, "Knuckles Right") == 0 ||
	           strcmp(d->firmware.model_number, "Knuckles EV3.0 Right") == 0) {
		d->variant = CONTROLLER_INDEX_RIGHT;
		VIVE_DEBUG(d, "Found Knuckles Right controller");
	} else if (strcmp(d->firmware.model_number, "Knuckles Left") == 0 ||
	           strcmp(d->firmware.model_number, "Knuckles EV3.0 Left") == 0) {
		d->variant = CONTROLLER_INDEX_LEFT;
		VIVE_DEBUG(d, "Found Knuckles Left controller");
	} else if (strcmp(d->firmware.model_number, "Vive Tracker PVT") == 0 ||
	           strcmp(d->firmware.model_number, "Vive. Tracker MV") == 0 ||
	           strcmp(d->firmware.model_number, "Vive Tracker MV") == 0) {
		d->variant = CONTROLLER_TRACKER_GEN1;
		VIVE_DEBUG(d, "Found Gen 1 tracker.");
	} else if (strcmp(d->firmware.model_number, "VIVE Tracker Pro MV") == 0) {
		d->variant = CONTROLLER_TRACKER_GEN2;
		VIVE_DEBUG(d, "Found Gen 2 tracker.");
	} else {
		VIVE_ERROR(d, "Failed to parse controller variant");
	}

	switch (d->variant) {
	case CONTROLLER_VIVE_WAND:
	case CONTROLLER_TRACKER_GEN1: {
		JSON_VEC3(json, "acc_bias", &d->imu.acc_bias);
		JSON_VEC3(json, "acc_scale", &d->imu.acc_scale);
		JSON_VEC3(json, "gyro_bias", &d->imu.gyro_bias);
		JSON_VEC3(json, "gyro_scale", &d->imu.gyro_scale);
		JSON_STRING(json, "mb_serial_number", d->firmware.mb_serial_number);
	} break;
	case CONTROLLER_INDEX_LEFT:
	case CONTROLLER_INDEX_RIGHT:
	case CONTROLLER_TRACKER_GEN2: {
		const cJSON *imu = u_json_get(json, "imu");
		_get_pose_from_pos_x_z(imu, &d->imu.trackref);

		JSON_VEC3(imu, "acc_bias", &d->imu.acc_bias);
		JSON_VEC3(imu, "acc_scale", &d->imu.acc_scale);
		JSON_VEC3(imu, "gyro_bias", &d->imu.gyro_bias);

		if (d->variant == CONTROLLER_TRACKER_GEN2)
			JSON_VEC3(imu, "gyro_scale", &d->imu.gyro_scale);
	} break;
	default: VIVE_ERROR(d, "Unknown Vive watchman variant."); return false;
	}

	JSON_STRING(json, "device_serial_number", d->firmware.device_serial_number);

	cJSON_Delete(json);

	// clang-format off
	VIVE_DEBUG(d, "= Vive controller configuration =");

	VIVE_DEBUG(d, "model_number: %s", d->firmware.model_number);
	VIVE_DEBUG(d, "mb_serial_number: %s", d->firmware.mb_serial_number);
	VIVE_DEBUG(d, "device_serial_number: %s", d->firmware.device_serial_number);

	if (d->ll <= U_LOGGING_DEBUG) {
		_print_vec3("acc_bias", &d->imu.acc_bias);
		_print_vec3("acc_scale", &d->imu.acc_scale);
		_print_vec3("gyro_bias", &d->imu.gyro_bias);
		_print_vec3("gyro_scale", &d->imu.gyro_scale);
	}

	// clang-format on

	return true;
}
