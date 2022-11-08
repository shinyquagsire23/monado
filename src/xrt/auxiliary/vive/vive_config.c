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

#include "util/u_debug.h"
#include "util/u_misc.h"
#include "util/u_json.h"
#include "util/u_distortion_mesh.h"

#include "math/m_api.h"

#include "tracking/t_tracking.h"
#include "math/m_vec3.h"
#include "math/m_space.h"


#define VIVE_TRACE(d, ...) U_LOG_IFL_T(d->log_level, __VA_ARGS__)
#define VIVE_DEBUG(d, ...) U_LOG_IFL_D(d->log_level, __VA_ARGS__)
#define VIVE_INFO(d, ...) U_LOG_IFL_I(d->log_level, __VA_ARGS__)
#define VIVE_WARN(d, ...) U_LOG_IFL_W(d->log_level, __VA_ARGS__)
#define VIVE_ERROR(d, ...) U_LOG_IFL_E(d->log_level, __VA_ARGS__)

#define JSON_INT(a, b, c) u_json_get_int(u_json_get(a, b), c)
#define JSON_FLOAT(a, b, c) u_json_get_float(u_json_get(a, b), c)
#define JSON_DOUBLE(a, b, c) u_json_get_double(u_json_get(a, b), c)
#define JSON_VEC3(a, b, c) u_json_get_vec3_array(u_json_get(a, b), c)
#define JSON_MATRIX_3X3(a, b, c) u_json_get_matrix_3x3(u_json_get(a, b), c)
#define JSON_STRING(a, b, c) u_json_get_string_into_array(u_json_get(a, b), c, sizeof(c))

DEBUG_GET_ONCE_BOOL_OPTION(vive_use_factory_rotations, "VIVE_USE_FACTORY_ROTATIONS", false)

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
	struct xrt_vec3 plus_x;
	struct xrt_vec3 plus_z;
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
	d->lh.sensor_count = map_size;


	// Transform the sensors into IMU space.
	struct xrt_pose trackref_to_imu = XRT_POSE_IDENTITY;
	math_pose_invert(&d->imu.trackref, &trackref_to_imu);

	for (i = 0; i < d->lh.sensor_count; i++) {
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
	}
	if (!succeeded_parsing_json) {
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
	struct xrt_pose invert;
	struct xrt_pose left_in_right;
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
                                   struct t_stereo_camera_calibration **calibration_ptr_to_ref,
                                   struct xrt_pose *out_head_in_left_camera)
{
	if (!d->cameras.valid) {
		U_LOG_E("Camera config not loaded, can not produce camera calibration.");
		return false;
	}

	struct index_camera *cameras = d->cameras.view;
	struct t_stereo_camera_calibration *calib = NULL;

	t_stereo_camera_calibration_alloc(&calib, 5);

	for (int i = 0; i < 2; i++) {
		calib->view[i].image_size_pixels.w = cameras[i].intrinsics.image_size_pixels.w;
		calib->view[i].image_size_pixels.h = cameras[i].intrinsics.image_size_pixels.h;

		// This better be row-major!
		calib->view[i].intrinsics[0][0] = cameras[i].intrinsics.focal_x;
		calib->view[i].intrinsics[0][1] = 0.0f;
		calib->view[i].intrinsics[0][2] = cameras[i].intrinsics.center_x;

		calib->view[i].intrinsics[1][0] = 0.0f;
		calib->view[i].intrinsics[1][1] = cameras[i].intrinsics.focal_y;
		calib->view[i].intrinsics[1][2] = cameras[i].intrinsics.center_y;

		calib->view[i].intrinsics[2][0] = 0.0f;
		calib->view[i].intrinsics[2][1] = 0.0f;
		calib->view[i].intrinsics[2][2] = 1.0f;

		calib->view[i].use_fisheye = true;
		calib->view[i].distortion_fisheye[0] = cameras[i].intrinsics.distortion[0];
		calib->view[i].distortion_fisheye[1] = cameras[i].intrinsics.distortion[1];
		calib->view[i].distortion_fisheye[2] = cameras[i].intrinsics.distortion[2];
		calib->view[i].distortion_fisheye[3] = cameras[i].intrinsics.distortion[3];
	}

	struct xrt_vec3 pos = d->cameras.opencv.position;
	struct xrt_vec3 x = XRT_VEC3_UNIT_X;
	struct xrt_vec3 y = XRT_VEC3_UNIT_Y;
	struct xrt_vec3 z = XRT_VEC3_UNIT_Z;
	math_quat_rotate_vec3(&d->cameras.opencv.orientation, &x, &x);
	math_quat_rotate_vec3(&d->cameras.opencv.orientation, &y, &y);
	math_quat_rotate_vec3(&d->cameras.opencv.orientation, &z, &z);

	calib->camera_translation[0] = pos.x;
	calib->camera_translation[1] = pos.y;
	calib->camera_translation[2] = pos.z;

	calib->camera_rotation[0][0] = x.x;
	calib->camera_rotation[0][1] = x.y;
	calib->camera_rotation[0][2] = x.z;

	calib->camera_rotation[1][0] = y.x;
	calib->camera_rotation[1][1] = y.y;
	calib->camera_rotation[1][2] = y.z;

	calib->camera_rotation[2][0] = z.x;
	calib->camera_rotation[2][1] = z.y;
	calib->camera_rotation[2][2] = z.z;

	math_pose_invert(&d->cameras.view[0].headref, out_head_in_left_camera);

	// Correctly reference count.
	t_stereo_camera_calibration_reference(calibration_ptr_to_ref, calib);
	t_stereo_camera_calibration_reference(&calib, NULL);

	return true;
}

struct t_imu_calibration
vive_get_imu_calibration(struct vive_config *d)
{

	struct xrt_vec3 ab = d->imu.acc_bias;
	struct xrt_vec3 as = d->imu.acc_scale;
	struct xrt_vec3 gb = d->imu.gyro_bias;
	struct xrt_vec3 gs = d->imu.gyro_scale;

	struct t_imu_calibration calib = {
	    .accel =
	        {
	            .transform = {{as.x, 0, 0}, {0, as.y, 0}, {0, 0, as.z}},
	            .offset = {-ab.x, -ab.y, -ab.z}, // negative because slam system will add, not subtract
	            .bias_std = {0.001, 0.001, 0.001},
	            .noise_std = {0.016, 0.016, 0.016},
	        },
	    .gyro =
	        {
	            .transform = {{gs.x, 0, 0}, {0, gs.y, 0}, {0, 0, gs.z}},
	            .offset = {-gb.x, -gb.y, -gb.z}, // negative because slam system will add, not subtract
	            .bias_std = {0.0001, 0.0001, 0.0001},
	            .noise_std = {0.000282, 0.000282, 0.000282},
	        },
	};
	return calib;
}

//! IMU extrinsics and frequencies
struct t_slam_calib_extras
vive_get_extra_calibration(struct vive_config *d)
{
	VIVE_ERROR(d, "Using default factory extrinsics data for vive driver.");
	VIVE_ERROR(d, "The rotations of the sensors in the factory data are off.");
	VIVE_ERROR(d, "Use a custom calibration instead whenever possible.");

	struct xrt_pose P_tr_imu = d->imu.trackref;
	struct xrt_pose P_tr_cam0 = d->cameras.view[0].trackref;
	struct xrt_pose P_tr_cam1 = d->cameras.view[1].trackref;
	struct xrt_pose P_imu_tr = {0};
	struct xrt_quat Q_imu_tr = {0};
	struct xrt_quat Q_tr_oxr = {.x = 0, .y = 1, .z = 0, .w = 0}; // TR is X: Left, Y: Up, Z: Forward
	struct xrt_quat Q_imu_oxr = {0};
	struct xrt_pose P_imu_imuxr = {0};
	struct xrt_pose P_imuxr_imu = {0};
	struct xrt_quat Q_tr_camslam = {.x = 0, .y = 0, .z = 1, .w = 0}; // Many follow X: Right, Y: Down, Z: Forward
	struct xrt_pose P_cam0_tr = {0};
	struct xrt_quat Q_cam0_tr = {0};
	struct xrt_quat Q_cam0_camslam = {0};
	struct xrt_pose P_cam0_cam0slam = {0};
	struct xrt_pose P_cam1_tr = {0};
	struct xrt_quat Q_cam1_tr = {0};
	struct xrt_quat Q_cam1_camslam = {0};
	struct xrt_pose P_cam1_cam1slam = {0};
	struct xrt_pose P_imu_cam0 = {0};
	struct xrt_pose P_imuxr_cam0 = {0};
	struct xrt_pose P_imuxr_cam0slam = {0};
	struct xrt_pose P_imu_cam1 = {0};
	struct xrt_pose P_imuxr_cam1 = {0};
	struct xrt_pose P_imuxr_cam1slam = {0};
	struct xrt_matrix_4x4 T_imu_cam0 = {0};
	struct xrt_matrix_4x4 T_imu_cam1 = {0};

	// Compute P_imuxr_imu. imuxr is same entity as IMU but with axes like OpenXR
	// E.g., for Index IMU has X: down, Y: left, Z: forward
	math_pose_invert(&P_tr_imu, &P_imu_tr);
	Q_imu_tr = P_imu_tr.orientation;
	math_quat_rotate(&Q_imu_tr, &Q_tr_oxr, &Q_imu_oxr);
	P_imu_imuxr.orientation = Q_imu_oxr;
	math_pose_invert(&P_imu_imuxr, &P_imuxr_imu);

	// Compute P_imuxr_cam0slam. cam0slam is the same entity as cam0 but with axes
	// like the SLAM system expects cameras to be. Usually X: Right, Y: Down, Z: Forward.
	math_pose_invert(&P_tr_cam0, &P_cam0_tr);
	Q_cam0_tr = P_cam0_tr.orientation;
	math_quat_rotate(&Q_cam0_tr, &Q_tr_camslam, &Q_cam0_camslam);
	P_cam0_cam0slam.orientation = Q_cam0_camslam;
	math_pose_transform(&P_imu_tr, &P_tr_cam0, &P_imu_cam0);
	math_pose_transform(&P_imuxr_imu, &P_imu_cam0, &P_imuxr_cam0);
	math_pose_transform(&P_imuxr_cam0, &P_cam0_cam0slam, &P_imuxr_cam0slam);

	// Compute P_imuxr_cam1slam. Same idea as P_imuxr_cam0slam
	math_pose_invert(&P_tr_cam1, &P_cam1_tr);
	Q_cam1_tr = P_cam1_tr.orientation;
	math_quat_rotate(&Q_cam1_tr, &Q_tr_camslam, &Q_cam1_camslam);
	P_cam1_cam1slam.orientation = Q_cam1_camslam;
	math_pose_transform(&P_imu_tr, &P_tr_cam1, &P_imu_cam1);
	math_pose_transform(&P_imuxr_imu, &P_imu_cam1, &P_imuxr_cam1);
	math_pose_transform(&P_imuxr_cam1, &P_cam1_cam1slam, &P_imuxr_cam1slam);

	bool use_factory_rots = debug_get_bool_option_vive_use_factory_rotations();
	if (!use_factory_rots) {
		//! @todo: Index factory calibration is weird and doesn't seem to have the
		//! proper extrinsics. Let's overwrite them with some extrinsics
		//! I got from doing a calibration on my own headset. These seem to work
		//! better than native values. (@mateosss)
		P_imuxr_cam0slam.orientation.x = 0.999206844251353;
		P_imuxr_cam0slam.orientation.y = -0.008523559718599975;
		P_imuxr_cam0slam.orientation.z = -0.038897421992888748;
		P_imuxr_cam0slam.orientation.w = 0.00014796379001732346;

		P_imuxr_cam1slam.orientation.x = 0.9990931516177515;
		P_imuxr_cam1slam.orientation.y = -0.011906493530393766;
		P_imuxr_cam1slam.orientation.z = 0.03990451825243117;
		P_imuxr_cam1slam.orientation.w = 0.008873512571741;
	}

	// Convert to 4x4 SE(3) matrices
	math_matrix_4x4_isometry_from_pose(&P_imuxr_cam0slam, &T_imu_cam0);
	math_matrix_4x4_isometry_from_pose(&P_imuxr_cam1slam, &T_imu_cam1);

	// Can we avoid hardcoding these?
	const int CAMERA_FREQUENCY = 54;
	const int IMU_FREQUENCY = 1000;

	struct t_slam_calib_extras calib = {
	    .imu_frequency = IMU_FREQUENCY,
	    .cams =
	        {
	            {.frequency = CAMERA_FREQUENCY, .T_imu_cam = T_imu_cam0},
	            {.frequency = CAMERA_FREQUENCY, .T_imu_cam = T_imu_cam1},
	        },
	};
	return calib;
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
vive_config_parse(struct vive_config *d, char *json_string, enum u_logging_level log_level)
{
	d->log_level = log_level;
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
	} else if (u_json_get(json, "model_name")) {
		JSON_STRING(json, "model_name", d->firmware.model_number);
	} else {
		VIVE_ERROR(d, "Could not find either 'model_number' or 'model_name' fields!");
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
	} else if (strcmp(d->firmware.model_number, "Vive_Pro MV") == 0 ||
	           strcmp(d->firmware.model_number, "VIVE_Pro MV") == 0) {
		d->variant = VIVE_VARIANT_PRO;
		VIVE_DEBUG(d, "Found HTC Vive Pro HMD");
	} else {
		VIVE_ERROR(d, "Failed to parse Vive HMD variant!\n\tfirmware.model_[number|name]: '%s'",
		           d->firmware.model_number);
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

	if (d->log_level <= U_LOGGING_DEBUG) {
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
		config->lh.sensor_count = 0;
	}
}

bool
vive_config_parse_controller(struct vive_controller_config *d, char *json_string, enum u_logging_level log_level)
{
	d->log_level = log_level;
	VIVE_DEBUG(d, "JSON config:\n%s", json_string);

	cJSON *json = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json)) {
		VIVE_ERROR(d, "Could not parse JSON data.");
		return false;
	}


	if (u_json_get(json, "model_number")) {
		JSON_STRING(json, "model_number", d->firmware.model_number);
	} else if (u_json_get(json, "model_name")) {
		JSON_STRING(json, "model_name", d->firmware.model_number);
	} else {
		VIVE_ERROR(d, "Could not find either 'model_number' or 'model_name' fields!");
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
		VIVE_ERROR(d, "Failed to parse controller variant!\n\tfirmware.model_[number|name]: '%s'",
		           d->firmware.model_number);
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

	if (d->log_level <= U_LOGGING_DEBUG) {
		_print_vec3("acc_bias", &d->imu.acc_bias);
		_print_vec3("acc_scale", &d->imu.acc_scale);
		_print_vec3("gyro_bias", &d->imu.gyro_bias);
		_print_vec3("gyro_scale", &d->imu.gyro_scale);
	}

	// clang-format on

	return true;
}
