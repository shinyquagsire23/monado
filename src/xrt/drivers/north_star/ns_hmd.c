// Copyright 2020, Collabora, Ltd.
// Copyright 2020, Nova King.
// Copyright 2020, Moses Turner.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  North Star HMD code.
 * @author Nova King <technobaboo@gmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <mosesturner@protonmail.com>
 * @author Nico Zobernig <nico.zobernig@gmail.com>
 * @ingroup drv_ns
 */

#include "math/m_mathinclude.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "os/os_time.h"

#include "ns_hmd.h"

#include "util/u_var.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_time.h"

#include "math/m_space.h"

DEBUG_GET_ONCE_LOG_OPTION(ns_log, "NS_LOG", U_LOGGING_INFO)

#define printf_pose(pose)                                                                                              \
	printf("%f %f %f  %f %f %f %f\n", pose.position.x, pose.position.y, pose.position.z, pose.orientation.x,       \
	       pose.orientation.y, pose.orientation.z, pose.orientation.w);


static float
try_get_ipd(struct ns_hmd *ns, const struct cJSON *json)
{ //
	const char *things[] = {"baseline", "ipd", "IPD"};
	bool done = false;
	float out;
	const char *thing;
	for (int i = 0; (!done && (i < 3)); i++) {
		thing = things[i];
		done = u_json_get_float(u_json_get(json, thing), &out);
	}
	if (!done) {
		NS_INFO(ns,
		        "No key `baseline (or ipd, or IPD)` in your config file. Guessing the IPD is 64 millimeters");
		out = 64.0f;
	}
	if (out > 250.0f) {
		NS_ERROR(ns, "IPD is way too high (%f millimeters!) Are you sure `%s` in your config file is correct?",
		         out, thing);
	}
	if (out < 10.0f) {
		NS_ERROR(ns, "IPD is way too low (%f millimeters!) Are you sure `%s` in your config file is correct?",
		         out, thing);
	}
	out *= 0.001f;
	NS_DEBUG(ns, "IPD returned is %f meters", out);

	return out;
}

static void
try_get_fov(struct ns_hmd *ns, const struct cJSON *json, struct xrt_fov *left_fov, struct xrt_fov *right_fov)
{
	const char *things[] = {"fov", "FOV"};
	float out_float;
	struct xrt_fov out_fov;
	const char *thing;
	for (int i = 0; (i < 2); i++) {
		thing = things[i];
		const cJSON *fov_obj = u_json_get(json, thing);
		if (fov_obj == NULL) {
			continue;
		}
		if (u_json_get_float_array(fov_obj, &out_fov.angle_left, 4)) { // LRTB array of floats, this is allowed.
			goto good;
		}
		if (u_json_get_float(fov_obj, &out_float)) {
			out_fov.angle_left = -out_float;
			out_fov.angle_right = out_float;
			out_fov.angle_up = out_float;
			out_fov.angle_down = -out_float;
			goto good;
		}
	}
	// Defaults, get skipped over if we found a FOV in the json
	NS_INFO(ns, "No key `fov` in your config file. Guessing you want 0.7 radian half-angles.");
	out_fov.angle_left = -0.7f;
	out_fov.angle_right = 0.7f;
	out_fov.angle_up = 0.7f;
	out_fov.angle_down = -0.7f;

good:
	assert(out_fov.angle_right > out_fov.angle_left);
	assert(out_fov.angle_up > out_fov.angle_down);
	assert(fabsf(out_fov.angle_up) < M_PI_2);
	assert(fabsf(out_fov.angle_down) < M_PI_2);
	assert(fabsf(out_fov.angle_left) < M_PI_2);
	assert(fabsf(out_fov.angle_right) < M_PI_2);
	memcpy(left_fov, &out_fov, sizeof(struct xrt_fov));
	memcpy(right_fov, &out_fov, sizeof(struct xrt_fov));
}

bool
ns_vipd_mesh_calc(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct ns_hmd *ns = ns_hmd(xdev);
	return u_compute_distortion_ns_vipd(&ns->dist_vipd, view, u, v, result);
}

bool
ns_vipd_parse(struct ns_hmd *ns)
{

	struct u_ns_vipd_values *temp_data = &ns->dist_vipd;
	const struct cJSON *config_json = ns->config_json;

	const cJSON *grids_json = u_json_get(config_json, "grids");
	if (grids_json == NULL)
		goto cleanup_vipd;

	const cJSON *current_element = NULL;
	char *current_key = NULL;

	cJSON_ArrayForEach(current_element, grids_json)
	{ // Note to people reviewing this: this is definitely not super safe. Tried to add as many null-checks as
	  // possible etc. but is probably a waste of time, it takes a while to do this right and the only person using
	  // this code is me -Moses
		current_key = current_element->string;
		float ipd = strtof(current_key, NULL) / 1000;
		if (!((ipd < .100) && (ipd > .030))) {
			U_LOG_E("Nonsense IPD in grid %d, skipping", temp_data->number_of_ipds + 1);
			continue;
		}

		temp_data->number_of_ipds += 1;
		temp_data->ipds = realloc(temp_data->ipds, temp_data->number_of_ipds * sizeof(float));
		temp_data->ipds[temp_data->number_of_ipds - 1] = ipd;
		temp_data->grids = realloc(temp_data->grids, temp_data->number_of_ipds * sizeof(struct u_ns_vipd_grid));

		for (int view = 0; view <= 1; view++) {
			const struct cJSON *grid_root = u_json_get(current_element, view ? "right" : "left");
			// if view is 0, then left. if view is 1, then right
			for (int lv = 0; lv < 65; lv++) {
				struct cJSON *v_axis = cJSON_GetArrayItem(grid_root, lv);

				for (int lu = 0; lu < 65; lu++) {
					struct cJSON *cell = cJSON_GetArrayItem(v_axis, lu + 1);

					struct cJSON *cellX = cJSON_GetArrayItem(cell, 0);
					struct cJSON *cellY = cJSON_GetArrayItem(cell, 1);
					if (grid_root == NULL || cell == NULL || v_axis == NULL || cellX == NULL ||
					    cellY == NULL) {
						NS_ERROR(ns,
						         "VIPD distortion config is malformed in some way, bailing.");
						goto cleanup_vipd;
					}
					temp_data->grids[temp_data->number_of_ipds - 1].grid[view][lv][lu].x =
					    (float)cellX->valuedouble;
					temp_data->grids[temp_data->number_of_ipds - 1].grid[view][lv][lu].y =
					    (float)cellY->valuedouble;
				}
			}
		}
	}

	float baseline = try_get_ipd(ns, config_json);

	struct u_ns_vipd_grid *high_grid = {0};
	struct u_ns_vipd_grid *low_grid = {0};
	float interp = 0;
	for (int i = 1; i < temp_data->number_of_ipds; i++) {
		NS_DEBUG(ns, "looking at %f lower and %f upper\n", temp_data->ipds[i - 1], temp_data->ipds[i]);
		if ((baseline >= temp_data->ipds[i - 1]) && (baseline <= temp_data->ipds[i])) {
			NS_DEBUG(ns, "okay, IPD is between %f and %f\n", temp_data->ipds[i - 1], temp_data->ipds[i]);
			high_grid = &temp_data->grids[i - 1];
			low_grid = &temp_data->grids[i];
			interp = math_map_ranges(baseline, temp_data->ipds[i - 1], temp_data->ipds[i], 0, 1);
			NS_DEBUG(ns, "interp is %f\n", interp);
			break;
		}
	}

	for (int view = 0; view <= 1; view++) {
		for (int lv = 0; lv < 65; lv++) {
			for (int lu = 0; lu < 65; lu++) {
				temp_data->grid_for_use.grid[view][lv][lu].x = math_map_ranges(
				    interp, 0, 1, low_grid->grid[view][lv][lu].x, high_grid->grid[view][lv][lu].x);
				temp_data->grid_for_use.grid[view][lv][lu].y = math_map_ranges(
				    interp, 0, 1, low_grid->grid[view][lv][lu].y, high_grid->grid[view][lv][lu].y);
			}
		}
	}

	try_get_fov(ns, config_json, &temp_data->fov[0], &temp_data->fov[1]);

	// stupid
	memcpy(&ns->base.hmd->distortion.fov[0], &temp_data->fov[0], sizeof(struct xrt_fov));
	memcpy(&ns->base.hmd->distortion.fov[1], &temp_data->fov[1], sizeof(struct xrt_fov));

	printf("%f %f %f %f\n", ns->base.hmd->distortion.fov[1].angle_down, ns->base.hmd->distortion.fov[1].angle_left,
	       ns->base.hmd->distortion.fov[1].angle_right, ns->base.hmd->distortion.fov[1].angle_up);

	ns->head_pose_to_eye[0].orientation.x = 0.0f;
	ns->head_pose_to_eye[0].orientation.y = 0.0f;
	ns->head_pose_to_eye[0].orientation.z = 0.0f;
	ns->head_pose_to_eye[0].orientation.w = 1.0f;
	ns->head_pose_to_eye[0].position.x = -baseline / 2;
	ns->head_pose_to_eye[0].position.y = 0.0f;
	ns->head_pose_to_eye[0].position.z = 0.0f;



	ns->head_pose_to_eye[1].orientation.x = 0.0f;
	ns->head_pose_to_eye[1].orientation.y = 0.0f;
	ns->head_pose_to_eye[1].orientation.z = 0.0f;
	ns->head_pose_to_eye[1].orientation.w = 1.0f;
	ns->head_pose_to_eye[1].position.x = baseline / 2;
	ns->head_pose_to_eye[1].position.y = 0.0f;
	ns->head_pose_to_eye[1].position.z = 0.0f;

	ns->base.compute_distortion = &ns_vipd_mesh_calc;

	return true;

cleanup_vipd:
	memset(&ns->dist_vipd, 0, sizeof(struct u_ns_vipd_values));
	return false;
}

/*
 *
 * "2D Polynomial" distortion; original implementation by Johnathon Zelstadt
 * Sometimes known as "v2", filename is often NorthStarCalibration.json
 *
 */

static bool
ns_p2d_mesh_calc(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct ns_hmd *ns = ns_hmd(xdev);
	return u_compute_distortion_ns_p2d(&ns->dist_p2d, view, u, v, result);
}


bool
ns_p2d_parse(struct ns_hmd *ns)
{

	struct xrt_pose temp_eyes_center_to_eye[2];

	// convenience names
	const struct cJSON *config_json = ns->config_json;

	// Note that x and y are flipped. We have to flip 'em at some point - the polynomial calibrator has a strange
	// definition of x and y. "opencv treats column major over row major (as in, Y,X for image look up)" -Dr. Damo
	if (u_json_get_float_array(u_json_get(config_json, "left_uv_to_rect_x"), ns->dist_p2d.y_coefficients_right,
	                           16) != 16)
		goto cleanup_p2d;
	if (u_json_get_float_array(u_json_get(config_json, "left_uv_to_rect_y"), ns->dist_p2d.x_coefficients_right,
	                           16) != 16)
		goto cleanup_p2d;
	if (u_json_get_float_array(u_json_get(config_json, "right_uv_to_rect_x"), ns->dist_p2d.y_coefficients_left,
	                           16) != 16)
		goto cleanup_p2d;
	if (u_json_get_float_array(u_json_get(config_json, "right_uv_to_rect_y"), ns->dist_p2d.x_coefficients_left,
	                           16) != 16)
		goto cleanup_p2d;

	// at this point, locked into using this distortion method - we can touch anything and not worry about side
	// effects
	float baseline = try_get_ipd(ns, config_json);

	math_pose_identity(&temp_eyes_center_to_eye[0]);
	math_pose_identity(&temp_eyes_center_to_eye[1]);
	temp_eyes_center_to_eye[0].position.x = -baseline / 2;
	temp_eyes_center_to_eye[1].position.x = baseline / 2;

	try_get_fov(ns, config_json, &ns->dist_p2d.fov[0], &ns->dist_p2d.fov[1]);

	memcpy(&ns->base.hmd->distortion.fov[0], &ns->dist_p2d.fov[0], sizeof(struct xrt_fov));
	memcpy(&ns->base.hmd->distortion.fov[1], &ns->dist_p2d.fov[1], sizeof(struct xrt_fov));

	ns->base.compute_distortion = &ns_p2d_mesh_calc;
	memcpy(&ns->head_pose_to_eye, &temp_eyes_center_to_eye, sizeof(struct xrt_pose) * 2);

	return true;

cleanup_p2d:
	memset(&ns->dist_p2d, 0, sizeof(struct u_ns_p2d_values));
	return false;
}


/*
 *
 * "Original 3D" undistortion, by Leap Motion
 * Sometimes known as "v1", config file name is often "Calibration.json"
 *
 */

static bool
ns_3d_mesh_calc(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct ns_hmd *ns = ns_hmd(xdev);
	struct ns_3d_data *data = &ns->dist_3d;
	struct xrt_vec2 uv = {u, v};
	struct xrt_vec2 warped_uv = {0.0f, 0.0f};

	ns_3d_display_uv_to_render_uv(uv, &warped_uv, &data->eyes[view]);

	result->r.x = warped_uv.x;
	result->r.y = warped_uv.y;
	result->g.x = warped_uv.x;
	result->g.y = warped_uv.y;
	result->b.x = warped_uv.x;
	result->b.y = warped_uv.y;
	return true;
}

static void
ns_3d_fov_calculate(struct xrt_fov *fov, struct xrt_quat projection)
{
	// Million thanks to Nico Zobernig for figuring this out
	fov->angle_left = atanf(projection.x);
	fov->angle_right = atanf(projection.y);
	fov->angle_up = atanf(projection.z);
	fov->angle_down = atanf(projection.w);
}

/*
 *
 * Parse functions.
 *
 */

static bool
ns_3d_leap_parse(struct ns_3d_leap *leap, const struct cJSON *leap_data)
{
	u_json_get_string_into_array(u_json_get(leap_data, "name"), leap->name, 64);
	u_json_get_string_into_array(u_json_get(leap_data, "serial"), leap->serial, 64);
	if (!u_json_get_vec3(u_json_get(u_json_get(leap_data, "localPose"), "position"), &leap->pose.position))
		return false;
	if (!u_json_get_quat(u_json_get(u_json_get(leap_data, "localPose"), "rotation"), &leap->pose.orientation))
		return false;
	return true;
}

static bool
ns_3d_eye_parse(struct ns_3d_eye *eye, const struct cJSON *eye_data)
{
	if (!u_json_get_float(u_json_get(eye_data, "ellipseMinorAxis"), &eye->ellipse_minor_axis))
		return false;
	if (!u_json_get_float(u_json_get(eye_data, "ellipseMajorAxis"), &eye->ellipse_major_axis))
		return false;
	if (!u_json_get_vec3(u_json_get(eye_data, "screenForward"), &eye->screen_forward))
		return false;
	if (!u_json_get_vec3(u_json_get(eye_data, "screenPosition"), &eye->screen_position))
		return false;
	if (!u_json_get_vec3(u_json_get(eye_data, "eyePosition"), &eye->eye_pose.position))
		return false;
	if (!u_json_get_quat(u_json_get(eye_data, "eyeRotation"), &eye->eye_pose.orientation))
		return false;
	if (!u_json_get_quat(u_json_get(eye_data, "cameraProjection"), &eye->camera_projection))
		return false;
	for (int x = 0; x < 4; ++x) {
		for (int y = 0; y < 4; ++y) {
			char key[4];
			sprintf(key, "e%d%d", x, y);

			u_json_get_float(u_json_get(u_json_get(eye_data, "sphereToWorldSpace"), key),
			                 &eye->sphere_to_world_space.v[(x * 4) + y]);
			u_json_get_float(u_json_get(u_json_get(eye_data, "worldToScreenSpace"), key),
			                 &eye->world_to_screen_space.v[(x * 4) + y]);
		}
	}
	return true;
}

bool
ns_3d_parse(struct ns_hmd *ns)
{
	struct ns_3d_data *our_ns_3d_data = &ns->dist_3d;

	if (!ns_3d_eye_parse(&our_ns_3d_data->eyes[0], u_json_get(ns->config_json, "leftEye")))
		goto cleanup_l3d;
	if (!ns_3d_eye_parse(&our_ns_3d_data->eyes[1], u_json_get(ns->config_json, "rightEye")))
		goto cleanup_l3d;
	if (!ns_3d_leap_parse(&our_ns_3d_data->leap, u_json_get(ns->config_json, "leapTracker")))
		goto cleanup_l3d;

	// Locked in, okay to touch anything inside ns struct
	ns_3d_fov_calculate(&ns->base.hmd->distortion.fov[0], our_ns_3d_data->eyes[0].camera_projection);
	ns_3d_fov_calculate(&ns->base.hmd->distortion.fov[1], our_ns_3d_data->eyes[1].camera_projection);

	ns->head_pose_to_eye[0] = our_ns_3d_data->eyes[0].eye_pose; // Left eye.
	ns->head_pose_to_eye[1] = our_ns_3d_data->eyes[1].eye_pose; // Right eye.

	our_ns_3d_data->eyes[0].optical_system = ns_3d_create_optical_system(&our_ns_3d_data->eyes[0]);
	our_ns_3d_data->eyes[1].optical_system = ns_3d_create_optical_system(&our_ns_3d_data->eyes[1]);

	ns->base.compute_distortion = &ns_3d_mesh_calc;

	return true;

cleanup_l3d:
	memset(&ns->dist_3d, 0, sizeof(struct ns_3d_data));
	return false;
}

/*
 *
 * Common functions
 *
 */

static void
ns_hmd_destroy(struct xrt_device *xdev)
{
	struct ns_hmd *ns = ns_hmd(xdev);

	// Remove the variable tracking.
	u_var_remove_root(ns);

	u_device_free(&ns->base);
}

static void
ns_hmd_update_inputs(struct xrt_device *xdev)
{}

static void
ns_hmd_get_tracked_pose(struct xrt_device *xdev,
                        enum xrt_input_name name,
                        uint64_t at_timestamp_ns,
                        struct xrt_space_relation *out_relation)
{
	struct ns_hmd *ns = ns_hmd(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		NS_ERROR(ns, "unknown input name");
		return;
	}

	*out_relation = ns->no_tracker_relation; // you can change this using the debug gui
}

static void
ns_hmd_get_view_poses(struct xrt_device *xdev,
                      const struct xrt_vec3 *default_eye_relation,
                      uint64_t at_timestamp_ns,
                      uint32_t view_count,
                      struct xrt_space_relation *out_head_relation,
                      struct xrt_fov *out_fovs,
                      struct xrt_pose *out_poses)
{
	// Use this to take care of most stuff, then fix up below.
	u_device_get_view_poses(xdev, default_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
	                        out_poses);

	// Fix fix.
	struct ns_hmd *ns = ns_hmd(xdev);
	for (uint32_t i = 0; i < view_count && i < ARRAY_SIZE(ns->head_pose_to_eye); i++) {
		out_poses[i] = ns->head_pose_to_eye[i];
	}
}

static bool
ns_config_load(struct ns_hmd *ns, const char *config_path)
{
	// Get the path to the JSON file
	bool json_allocated = false;
	if (config_path == NULL || strcmp(config_path, "/") == 0) {
		NS_INFO(ns,
		        "Configuration path \"%s\" does not lead to a "
		        "configuration JSON file. Set the NS_CONFIG_PATH env "
		        "variable to your JSON.",
		        config_path);
		return false;
	}
	// Open the JSON file and put its contents into a string
	FILE *config_file = fopen(config_path, "r");
	if (config_file == NULL) {
		NS_INFO(ns, "The configuration file at path \"%s\" was unable to load", config_path);
		goto parse_error;
	}

	fseek(config_file, 0, SEEK_END);     // Go to end of file
	long file_size = ftell(config_file); // See offset we're at. This should be the file size in bytes.
	rewind(config_file);                 // Go back to the beginning of the file

	if (file_size == 0) {
		NS_INFO(ns, "Empty config file!");
		goto parse_error;
	} else if (file_size > 3 * pow(1024, 2)) { // 3 MiB
		NS_INFO(ns, "Huge config file! (%f MiB!!) Something's wrong here.", ((float)file_size) / pow(1024, 2));
		goto parse_error;
	}

	char *json = calloc(file_size + 1, 1);
	json_allocated = true;

	size_t ret = fread(json, 1, file_size, config_file);
	if ((long)ret != file_size) {
		NS_ERROR(ns, "Failed to read configuration file at path \"%s\"", config_path);
		goto parse_error;
	}
	fclose(config_file);
	config_file = NULL;
	json[file_size] = '\0';

	ns->config_json = cJSON_Parse(json);
	if (ns->config_json == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		NS_INFO(ns, "The JSON file at path \"%s\" was unable to parse", config_path);
		if (error_ptr != NULL) {
			NS_INFO(ns, "because of an error before %s", error_ptr);
		}
		goto parse_error;
	}

	// this function is not supposed to return true if ns->config_json is NULL
	assert(ns->config_json != NULL);
	free(json);

	return true;

parse_error:
	if (config_file != NULL) {
		fclose(config_file);
		config_file = NULL;
	}
	if (json_allocated) {
		free(json);
	}
	NS_INFO(ns, "Are you sure you're using the right configuration file?");
	return false;
}


/*
 *
 * Create function.
 *
 */

struct xrt_device *
ns_hmd_create(const char *config_path)
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct ns_hmd *ns = U_DEVICE_ALLOCATE(struct ns_hmd, flags, 1, 0);
	ns->log_level = debug_get_log_option_ns_log();

	if (!ns_config_load(ns, config_path))
		goto cleanup; // don't need to print any error, ns_config_load did that for us

	int number_wrap = 3; // number of elements in below array of function pointers. Const to stop compiler warnings.
	bool (*wrap_func_ptr[3])(struct ns_hmd *) = {ns_3d_parse, ns_p2d_parse, ns_vipd_parse};
	// C syntax is weird here. This is an array of pointers to functions with arguments (struct ns_system * system)
	// that all return a boolean value. The array should be roughly in descending order of how likely we think the
	// user means to use each method For now VIPD is last because Moses is the only one that uses it

	bool found_config_wrap = false;
	for (int i = 0; i < number_wrap; i++) {
		if (wrap_func_ptr[i](ns)) { // wrap_func_ptr[i](ns) is a function call!
			U_LOG_I("North Star: Using config wrap %i", i);
			found_config_wrap = true;
			break;
		}
	} // This will segfault at function ?? if you use GDB and the length is wrong.

	if (!found_config_wrap) {
		NS_INFO(ns, "North Star: Config file seems to be invalid.");
		goto cleanup;
	}

	ns->base.update_inputs = ns_hmd_update_inputs;
	ns->base.get_tracked_pose = ns_hmd_get_tracked_pose;
	ns->base.get_view_poses = ns_hmd_get_view_poses;
	ns->base.destroy = ns_hmd_destroy;
	ns->base.name = XRT_DEVICE_GENERIC_HMD;
	math_pose_identity(&ns->no_tracker_relation.pose);
	ns->no_tracker_relation.relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
	// Appeases the inner workings of Monado for when there's no head tracker and we're giving a fake pose through
	// the debug gui
	ns->base.orientation_tracking_supported = true;
	ns->base.position_tracking_supported = true;
	ns->base.device_type = XRT_DEVICE_TYPE_HMD;


	// Print name.
	snprintf(ns->base.str, XRT_DEVICE_NAME_LEN, "North Star");
	snprintf(ns->base.serial, XRT_DEVICE_NAME_LEN, "North Star");
	// Setup input.
	ns->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	struct u_extents_2d exts;

	// info.w_meters = 0.0588f * 2.0f;
	// info.h_meters = 0.0655f;

	// one NS screen is 1440px wide, but there are two of them.
	exts.w_pixels = 1440 * 2;
	// Both NS screens are 1600px tall
	exts.h_pixels = 1600;

	u_extents_2d_split_side_by_side(&ns->base, &exts);

	ns->base.hmd->distortion.models = XRT_DISTORTION_MODEL_COMPUTE;
	ns->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_COMPUTE;

	// Setup variable tracker.
	u_var_add_root(ns, "North Star", true);
	u_var_add_pose(ns, &ns->no_tracker_relation.pose, "pose");
	ns->base.orientation_tracking_supported = true;
	ns->base.device_type = XRT_DEVICE_TYPE_HMD;

	size_t idx = 0;
	// Preferred; almost all North Stars (as of early 2021) are see-through.
	ns->base.hmd->blend_modes[idx++] = XRT_BLEND_MODE_ADDITIVE;

	// XRT_BLEND_MODE_OPAQUE is not preferred and kind of a lie, but you can totally use North Star for VR apps,
	// despite its see-through display. And there's nothing stopping you from covering up the outside of the
	// reflector, turning it into an opaque headset. As most VR apps I've encountered require BLEND_MODE_OPAQUE to
	// be an option, we need to support it.
	ns->base.hmd->blend_modes[idx++] = XRT_BLEND_MODE_OPAQUE;

	// Not supporting ALPHA_BLEND for now, because I know nothing about it, have no reason to use it, and want to
	// avoid unintended consequences. As soon as you have a specific reason to support it, go ahead and support it.
	ns->base.hmd->blend_mode_count = idx;

	uint64_t start;
	uint64_t end;

	start = os_monotonic_get_ns();
	u_distortion_mesh_fill_in_compute(&ns->base);
	end = os_monotonic_get_ns();

	float diff = (end - start);
	diff /= U_TIME_1MS_IN_NS;

	NS_DEBUG(ns, "Filling mesh took %f ms", diff);


	return &ns->base;

cleanup:
	ns_hmd_destroy(&ns->base);
	return NULL;
}
