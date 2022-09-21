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

/*
 *
 * Printing functions.
 *
 */

#define NS_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->log_level, __VA_ARGS__)
#define NS_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->log_level, __VA_ARGS__)
#define NS_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->log_level, __VA_ARGS__)
#define NS_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->log_level, __VA_ARGS__)
#define NS_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->log_level, __VA_ARGS__)


static float
try_get_ipd(struct ns_hmd *ns, const struct cJSON *json)
{
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
		        "No key `baseline` (or `ipd`, or `IPD`) in your config file. "
		        "Guessing the IPD is 64 millimeters");
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
try_get_fov(struct ns_hmd *ns, const struct cJSON *json, struct xrt_fov *out_left_fov, struct xrt_fov *out_right_fov)
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
	*out_left_fov = out_fov;
	*out_right_fov = out_fov;
}



bool
ns_p2d_parse(struct ns_hmd *ns, const cJSON *json)
{
	struct u_ns_p2d_values *values = &ns->config.dist_p2d;

	// Note that x and y are flipped. We have to flip 'em at some point - the polynomial calibrator has a strange
	// definition of x and y. "opencv treats column major over row major (as in, Y,X for image look up)" -Dr. Damo
	if (u_json_get_float_array(u_json_get(json, "left_uv_to_rect_x"), values->y_coefficients_right, 16) != 16)
		goto cleanup_p2d;
	if (u_json_get_float_array(u_json_get(json, "left_uv_to_rect_y"), values->x_coefficients_right, 16) != 16)
		goto cleanup_p2d;
	if (u_json_get_float_array(u_json_get(json, "right_uv_to_rect_x"), values->y_coefficients_left, 16) != 16)
		goto cleanup_p2d;
	if (u_json_get_float_array(u_json_get(json, "right_uv_to_rect_y"), values->x_coefficients_left, 16) != 16)
		goto cleanup_p2d;

	// at this point, locked into using this distortion method - we can touch anything and not worry about side
	// effects
	ns->config.distortion_type = NS_DISTORTION_TYPE_POLYNOMIAL_2D;

	float baseline = try_get_ipd(ns, json);

	math_pose_identity(&ns->config.head_pose_to_eye[0]);
	math_pose_identity(&ns->config.head_pose_to_eye[1]);
	ns->config.head_pose_to_eye[0].position.x = -baseline / 2;
	ns->config.head_pose_to_eye[1].position.x = baseline / 2;

	try_get_fov(ns, json, &values->fov[0], &values->fov[1]);

	ns->config.fov[0] = values->fov[0];
	ns->config.fov[1] = values->fov[1];

	return true;

cleanup_p2d:
	return false;
}


static void
ns_3d_fov_calculate(struct xrt_quat projection, struct xrt_fov *out_fov)
{
	// Million thanks to Nico Zobernig for figuring this out
	out_fov->angle_left = atanf(projection.x);
	out_fov->angle_right = atanf(projection.y);
	out_fov->angle_up = atanf(projection.z);
	out_fov->angle_down = atanf(projection.w);
}

/*
 *
 * Parse functions.
 *
 */



static bool
ns_3d_eye_parse(struct ns_hmd *ns, struct ns_3d_eye *eye, const struct cJSON *eye_data)
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
ns_3d_parse(struct ns_hmd *ns, const cJSON *json)
{
	struct ns_3d_values *values = &ns->config.dist_3d;


	if (!ns_3d_eye_parse(ns, &values->eyes[0], u_json_get(json, "leftEye")))
		goto cleanup_l3d;
	if (!ns_3d_eye_parse(ns, &values->eyes[1], u_json_get(json, "rightEye")))
		goto cleanup_l3d;

	// Locked in, okay to touch anything inside ns struct
	ns->config.distortion_type = NS_DISTORTION_TYPE_GEOMETRIC_3D;

	ns_3d_fov_calculate(values->eyes[0].camera_projection, &ns->config.fov[0]);
	ns_3d_fov_calculate(values->eyes[1].camera_projection, &ns->config.fov[1]);

	ns->config.head_pose_to_eye[0] = values->eyes[0].eye_pose; // Left eye.
	ns->config.head_pose_to_eye[1] = values->eyes[1].eye_pose; // Right eye.

	values->eyes[0].optical_system = ns_3d_create_optical_system(&values->eyes[0]);
	values->eyes[1].optical_system = ns_3d_create_optical_system(&values->eyes[1]);

	return true;

cleanup_l3d:
	ns_3d_free_optical_system(&values->eyes[0].optical_system);
	ns_3d_free_optical_system(&values->eyes[1].optical_system);
	return false;
}


/*
 *
 * Moses Turner's meshgrid-based distortion correction
 *
 */

bool
ns_mt_parse(struct ns_hmd *ns, const cJSON *json)
{
	struct u_ns_meshgrid_values *values = &ns->config.dist_meshgrid;

	if (strcmp(cJSON_GetStringValue(u_json_get(json, "type")), "Moses Turner's distortion correction") != 0) {
		goto cleanup_mt;
	}
	int version = 0;
	u_json_get_int(u_json_get(json, "version"), &version);
	if (version != 2) {
		goto cleanup_mt;
	}

	u_json_get_int(u_json_get(json, "num_grid_points_x"), &values->num_grid_points_u);
	u_json_get_int(u_json_get(json, "num_grid_points_y"), &values->num_grid_points_v);

	values->grid[0] = U_TYPED_ARRAY_CALLOC(struct xrt_vec2, values->num_grid_points_u * values->num_grid_points_v);
	values->grid[1] = U_TYPED_ARRAY_CALLOC(struct xrt_vec2, values->num_grid_points_u * values->num_grid_points_v);

	values->ipd = try_get_ipd(ns, json);

	const cJSON *current_element = json;


	for (int view = 0; view <= 1; view++) {
		const struct cJSON *grid_root = u_json_get(current_element, view ? "right" : "left");
		grid_root = u_json_get(grid_root, "grid");
		// if view is 0, then left. if view is 1, then right
		for (int lv = 0; lv < values->num_grid_points_v; lv++) {
			struct cJSON *v_axis = cJSON_GetArrayItem(grid_root, lv);

			for (int lu = 0; lu < values->num_grid_points_u; lu++) {
				struct cJSON *cell = cJSON_GetArrayItem(v_axis, lu);

				struct cJSON *cellX = cJSON_GetArrayItem(cell, 0);
				struct cJSON *cellY = cJSON_GetArrayItem(cell, 1);
				if (grid_root == NULL || cell == NULL || v_axis == NULL || cellX == NULL ||
				    cellY == NULL) {
					NS_ERROR(ns, "Distortion config file is malformed in some way, bailing");
					goto cleanup_mt;
				}
				float *x_ptr = &values->grid[view][(lv * values->num_grid_points_u) + lu].x;
				float *y_ptr = &values->grid[view][(lv * values->num_grid_points_u) + lu].y;
				u_json_get_float(cellX, x_ptr);
				u_json_get_float(cellY, y_ptr);
			}
		}
	}
	// locked in
	ns->config.distortion_type = NS_DISTORTION_TYPE_MOSES_MESHGRID;

	float baseline = values->ipd;


	try_get_fov(ns, json, &values->fov[0], &values->fov[1]);

	ns->config.fov[0] = values->fov[0];
	ns->config.fov[1] = values->fov[1];

	math_pose_identity(&ns->config.head_pose_to_eye[0]);
	math_pose_identity(&ns->config.head_pose_to_eye[1]);
	ns->config.head_pose_to_eye[0].position.x = -baseline / 2;
	ns->config.head_pose_to_eye[1].position.x = baseline / 2;

	return true;

cleanup_mt:
	free(values->grid[0]);
	free(values->grid[1]);
	return false;
}



static bool
ns_optical_config_parse(struct ns_hmd *ns)
{
	if (ns_3d_parse(ns, ns->config_json)) {
		NS_INFO(ns, "Using Gemetric 3D display distortion correction!");
		return true;
	}
	if (ns_p2d_parse(ns, ns->config_json)) {
		NS_INFO(ns, "Using Polynomial 2D display distortion correction!");
		return true;
	}
	if (ns_mt_parse(ns, ns->config_json)) {
		NS_INFO(ns, "Using Moses's meshgrid-based display distortion correction!");
		return true;
	}
	U_LOG_E("Couldn't find a valid display distortion correction!");
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
	NS_DEBUG(ns, "Called!");

	// Remove the variable tracking.
	u_var_remove_root(ns);

	if (ns->config.distortion_type == NS_DISTORTION_TYPE_GEOMETRIC_3D) {
		ns_3d_free_optical_system(&ns->config.dist_3d.eyes[0].optical_system);
		ns_3d_free_optical_system(&ns->config.dist_3d.eyes[1].optical_system);
	} else if (ns->config.distortion_type == NS_DISTORTION_TYPE_MOSES_MESHGRID) {
		free(ns->config.dist_meshgrid.grid[0]);
		free(ns->config.dist_meshgrid.grid[1]);
	}

	u_device_free(&ns->base);
}

static void
ns_hmd_update_inputs(struct xrt_device *xdev)
{
	struct ns_hmd *ns = ns_hmd(xdev);
	NS_DEBUG(ns, "Called!");
}

static void
ns_hmd_get_tracked_pose(struct xrt_device *xdev,
                        enum xrt_input_name name,
                        uint64_t at_timestamp_ns,
                        struct xrt_space_relation *out_relation)
{
	struct ns_hmd *ns = ns_hmd(xdev);
	NS_DEBUG(ns, "Called!");

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
	struct ns_hmd *ns = ns_hmd(xdev);
	NS_DEBUG(ns, "Called!");

	// Use this to take care of most stuff, then fix up below.
	u_device_get_view_poses(xdev, default_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
	                        out_poses);

	// Fix fix.
	for (uint32_t i = 0; i < view_count && i < ARRAY_SIZE(ns->config.head_pose_to_eye); i++) {
		out_poses[i] = ns->config.head_pose_to_eye[i];
	}
}

bool
ns_mesh_calc(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct ns_hmd *ns = ns_hmd(xdev);
	NS_DEBUG(ns, "Called!");
	// struct xrt_vec2 warped_uv;
	switch (ns->config.distortion_type) {
	case NS_DISTORTION_TYPE_GEOMETRIC_3D: {
		struct xrt_vec2 uv = {u, v};
		struct xrt_vec2 warped_uv = {0.0f, 0.0f};

		ns_3d_display_uv_to_render_uv(uv, &warped_uv, &ns->config.dist_3d.eyes[view]);

		result->r.x = warped_uv.x;
		result->r.y = warped_uv.y;
		result->g.x = warped_uv.x;
		result->g.y = warped_uv.y;
		result->b.x = warped_uv.x;
		result->b.y = warped_uv.y;
		return true;
	}
	case NS_DISTORTION_TYPE_POLYNOMIAL_2D: {
		return u_compute_distortion_ns_p2d(&ns->config.dist_p2d, view, u, v, result);
	}
	case NS_DISTORTION_TYPE_MOSES_MESHGRID: {
		return u_compute_distortion_ns_meshgrid(&ns->config.dist_meshgrid, view, u, v, result);
	}
	default: {
		assert(false);
		return false;
	}
	}
}

/*
 *
 * Create function.
 *
 */

struct xrt_device *
ns_hmd_create(const cJSON *config_json)
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct ns_hmd *ns = U_DEVICE_ALLOCATE(struct ns_hmd, flags, 1, 0);

	ns->config_json = config_json;
	ns_optical_config_parse(ns);

	ns->log_level = debug_get_log_option_ns_log();
	NS_DEBUG(ns, "Called!");

	ns->base.hmd->distortion.fov[0] = ns->config.fov[0];
	ns->base.hmd->distortion.fov[1] = ns->config.fov[1];


	ns->base.compute_distortion = ns_mesh_calc;
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
}
