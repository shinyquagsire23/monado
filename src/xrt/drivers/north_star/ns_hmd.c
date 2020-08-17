// Copyright 2020, Collabora, Ltd.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  North Star HMD code.
 * @author Nova King <technobaboo@gmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ns
 */

#include "math/m_mathinclude.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "os/os_time.h"

#include "ns_hmd.h"

#include "util/u_var.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_time.h"
#include "util/u_distortion_mesh.h"

#include "xrt/xrt_config_drivers.h"
#ifdef XRT_BUILD_DRIVER_RS
#include "../realsense/rs_interface.h"
#endif


/*
 *
 * Functions
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
{
	struct ns_hmd *ns = ns_hmd(xdev);

	if (ns->tracker != NULL) {
		xrt_device_update_inputs(ns->tracker);
	}
}

static void
ns_hmd_get_tracked_pose(struct xrt_device *xdev,
                        enum xrt_input_name name,
                        uint64_t at_timestamp_ns,
                        uint64_t *out_relation_timestamp_ns,
                        struct xrt_space_relation *out_relation)
{
	struct ns_hmd *ns = ns_hmd(xdev);


	// If the tracking device is created use it.
	if (ns->tracker != NULL) {
		xrt_device_get_tracked_pose(ns->tracker, name, at_timestamp_ns,
		                            out_relation_timestamp_ns,
		                            out_relation);
		return;
	}

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		NS_ERROR(ns, "unknown input name");
		return;
	}

	uint64_t now = os_monotonic_get_ns();

	*out_relation_timestamp_ns = now;
	out_relation->pose = ns->pose;
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_POSITION_VALID_BIT);
}

static void
ns_hmd_get_view_pose(struct xrt_device *xdev,
                     struct xrt_vec3 *eye_relation,
                     uint32_t view_index,
                     struct xrt_pose *out_pose)
{
	struct ns_hmd *ns = ns_hmd(xdev);
	*out_pose = ns->eye_configs[view_index].eye_pose;
}


/*
 *
 * Mesh functions.
 *
 */

static void
ns_mesh_calc(struct u_uv_generator *gen,
             int view,
             float u,
             float v,
             struct u_uv_triplet *result)
{
	struct ns_mesh *mesh = ns_mesh(gen);

	struct ns_uv uv = {u, v};
	struct ns_uv warped_uv = {0.0f, 0.0f};
	ns_display_uv_to_render_uv(uv, &warped_uv,
	                           &mesh->ns->eye_configs[view]);

	result->r.x = warped_uv.u;
	result->r.y = warped_uv.v;
	result->g.x = warped_uv.u;
	result->g.y = warped_uv.v;
	result->b.x = warped_uv.u;
	result->b.y = warped_uv.v;
}

static void
ns_mesh_destroy(struct u_uv_generator *gen)
{
	struct ns_mesh *mesh = (struct ns_mesh *)gen;
	(void)mesh; // Noop
}

static void
ns_leap_parse(struct ns_leap *leap, struct cJSON *leap_data)
{
	/*
	        These are very wrong!
	        You could very likely write into random memory here.

	        u_json_get_string(cJSON_GetObjectItemCaseSensitive(leap_data,
	   "name"), &leap->name);
	        u_json_get_string(cJSON_GetObjectItemCaseSensitive(leap_data,
	   "serial"), &leap->serial);
	*/

	u_json_get_vec3(
	    cJSON_GetObjectItemCaseSensitive(
	        cJSON_GetObjectItemCaseSensitive(leap_data, "localPose"),
	        "position"),
	    &leap->pose.position);
	u_json_get_quat(
	    cJSON_GetObjectItemCaseSensitive(
	        cJSON_GetObjectItemCaseSensitive(leap_data, "localPose"),
	        "rotation"),
	    &leap->pose.orientation);
}

static void
ns_eye_parse(struct ns_eye *eye, struct cJSON *eye_data)
{
	u_json_get_float(
	    cJSON_GetObjectItemCaseSensitive(eye_data, "ellipseMinorAxis"),
	    &eye->ellipse_minor_axis);
	u_json_get_float(
	    cJSON_GetObjectItemCaseSensitive(eye_data, "ellipseMajorAxis"),
	    &eye->ellipse_major_axis);
	u_json_get_vec3(
	    cJSON_GetObjectItemCaseSensitive(eye_data, "screenForward"),
	    &eye->screen_forward);
	u_json_get_vec3(
	    cJSON_GetObjectItemCaseSensitive(eye_data, "screenPosition"),
	    &eye->screen_position);
	u_json_get_vec3(
	    cJSON_GetObjectItemCaseSensitive(eye_data, "eyePosition"),
	    &eye->eye_pose.position);
	u_json_get_quat(
	    cJSON_GetObjectItemCaseSensitive(eye_data, "eyeRotation"),
	    &eye->eye_pose.orientation);
	u_json_get_quat(
	    cJSON_GetObjectItemCaseSensitive(eye_data, "cameraProjection"),
	    &eye->camera_projection);
	for (int x = 0; x < 4; ++x) {
		for (int y = 0; y < 4; ++y) {
			char key[4];
			sprintf(key, "e%d%d", x, y);

			u_json_get_float(
			    cJSON_GetObjectItemCaseSensitive(
			        cJSON_GetObjectItemCaseSensitive(
			            eye_data, "sphereToWorldSpace"),
			        key),
			    &eye->sphere_to_world_space.v[(x * 4) + y]);
			u_json_get_float(
			    cJSON_GetObjectItemCaseSensitive(
			        cJSON_GetObjectItemCaseSensitive(
			            eye_data, "worldToScreenSpace"),
			        key),
			    &eye->world_to_screen_space.v[(x * 4) + y]);
		}
	}
}


/*
 *
 * Parse function.
 *
 */

static void
ns_fov_calculate(struct xrt_fov *fov, struct xrt_quat projection)
{
	fov->angle_up = projection.x; // atanf(fabsf(projection.x) /
	                              // near_plane);
	fov->angle_down =
	    projection.y; // atanf(fabsf(projection.y) / near_plane);
	fov->angle_left =
	    projection.z; // atanf(fabsf(projection.z) / near_plane);
	fov->angle_right =
	    projection.w; // atanf(fabsf(projection.w) / near_plane);
}

static bool
ns_config_load(struct ns_hmd *ns)
{
	// Get the path to the JSON file
	if (ns->config_path == NULL || strcmp(ns->config_path, "/") == 0) {
		NS_ERROR(ns,
		         "Configuration path \"%s\" does not lead to a "
		         "configuration JSON file. Set the NS_CONFIG_PATH env "
		         "variable to your JSON.",
		         ns->config_path);
		return false;
	}

	// Open the JSON file and put its contents into a string
	FILE *config_file = fopen(ns->config_path, "r");
	if (config_file == NULL) {
		NS_ERROR(
		    ns,
		    "The configuration file at path \"%s\" was unable to load",
		    ns->config_path);
		return false;
	}

	char json[8192];
	size_t i = 0;
	while (!feof(config_file) && i < (sizeof(json) - 1)) {
		json[i++] = fgetc(config_file);
	}
	json[i] = '\0';

	struct cJSON *config_json;

	// Parse the JSON file
	config_json = cJSON_Parse(json);
	if (config_json == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		NS_ERROR(ns, "The JSON file at path \"%s\" was unable to parse",
		         ns->config_path);
		if (error_ptr != NULL) {
			NS_ERROR(ns, "because of an error before %s",
			         error_ptr);
		}
		return false;
	}

	ns_eye_parse(&ns->eye_configs[0],
	             cJSON_GetObjectItemCaseSensitive(config_json, "leftEye"));
	ns_eye_parse(&ns->eye_configs[1],
	             cJSON_GetObjectItemCaseSensitive(config_json, "rightEye"));
	ns_leap_parse(&ns->leap_config, cJSON_GetObjectItemCaseSensitive(
	                                    config_json, "leapTracker"));
	cJSON_Delete(config_json);
	return true;
}


/*
 *
 * Create function.
 *
 */

struct xrt_device *
ns_hmd_create(const char *config_path, bool print_spew, bool print_debug)
{
	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)(
	    U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct ns_hmd *ns = U_DEVICE_ALLOCATE(struct ns_hmd, flags, 1, 0);
	ns->base.update_inputs = ns_hmd_update_inputs;
	ns->base.get_tracked_pose = ns_hmd_get_tracked_pose;
	ns->base.get_view_pose = ns_hmd_get_view_pose;
	ns->base.destroy = ns_hmd_destroy;
	ns->base.name = XRT_DEVICE_GENERIC_HMD;
	ns->pose.orientation.w = 1.0f; // All other values set to zero.
	ns->config_path = config_path;
	ns->print_spew = print_spew;
	ns->print_debug = print_debug;

	// Print name.
	snprintf(ns->base.str, XRT_DEVICE_NAME_LEN, "North Star");

	// Setup input.
	ns->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	// Setup info.
	struct u_device_simple_info info;
	info.display.w_pixels = 2880;
	info.display.h_pixels = 1440;
	info.display.w_meters = 0.13f;
	info.display.h_meters = 0.07f;
	info.lens_horizontal_separation_meters = 0.13f / 2.0f;
	info.lens_vertical_position_meters = 0.07f / 2.0f;
	info.views[0].fov = 70.0f * (M_PI / 180.0f);
	info.views[1].fov = 70.0f * (M_PI / 180.0f);

	if (!ns_config_load(ns))
		goto cleanup;

	ns_fov_calculate(&ns->base.hmd->views[0].fov,
	                 ns->eye_configs[0].camera_projection);
	ns_fov_calculate(&ns->base.hmd->views[1].fov,
	                 ns->eye_configs[1].camera_projection);

	// Create the optical systems
	ns->eye_configs[0].optical_system =
	    ns_create_optical_system(&ns->eye_configs[0]);
	ns->eye_configs[1].optical_system =
	    ns_create_optical_system(&ns->eye_configs[1]);

	// Setup the north star basic info
	if (!u_device_setup_split_side_by_side(&ns->base, &info)) {
		NS_ERROR(ns, "Failed to setup basic device info");
		goto cleanup;
	}

	// If built, try to load the realsense tracker.
#ifdef XRT_BUILD_DRIVER_RS
	ns->tracker = rs_6dof_create();
#endif

	// Setup variable tracker.
	u_var_add_root(ns, "North Star", true);
	u_var_add_pose(ns, &ns->pose, "pose");

	// Setup the distortion mesh.
	struct ns_mesh mesh;
	U_ZERO(&mesh);
	mesh.ns = ns;
	mesh.base.calc = ns_mesh_calc;
	mesh.base.destroy = ns_mesh_destroy;

	// Do the mesh generation.
	u_distortion_mesh_from_gen(&mesh.base, 2, ns->base.hmd);

	ns->base.orientation_tracking_supported = true;
	ns->base.position_tracking_supported = ns->tracker != NULL;
	ns->base.device_type = XRT_DEVICE_TYPE_HMD;

	return &ns->base;

cleanup:
	ns_hmd_destroy(&ns->base);
	return NULL;
}
