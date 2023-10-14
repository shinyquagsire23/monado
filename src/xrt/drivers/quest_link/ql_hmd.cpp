/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016, Philipp Zabel
 * Copyright 2019-2022, Jan Schmidt
 * Copyright 2022, Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright 2022, Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright 2022, Max Thomas
 * SPDX-License-Identifier: BSL-1.0
 *
 */
/*!
 * @file
 * @brief  Driver code for Meta Quest Link headsets
 *
 * Implementation for the HMD communication, calibration and
 * IMU integration.
 *
 * Ported from OpenHMD
 *
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_quest_link
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <inttypes.h>
#include <algorithm>
#include <optional>

#include "math/m_api.h"
#include "math/m_vec3.h"
#include "math/m_space.h"
#include "math/m_predict.h"
#include "math/m_filter_one_euro.h"

#include "os/os_time.h"

#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_trace_marker.h"
#include "util/u_time.h"
#include "util/u_var.h"

#include "xrt/xrt_device.h"

#include "ql_hmd.h"
#include "ql_system.h"

#include "ql_comp_target.h"

DEBUG_GET_ONCE_NUM_OPTION(mesh_size, "XRT_MESH_SIZE", 64)

static void
ql_update_inputs(struct xrt_device *xdev)
{}

static void ql_hmd_create_compositor_target(struct xrt_device * xdev,
                                               struct comp_compositor * comp,
                                               const struct comp_target_factory ** out_target);

static void
ql_get_tracked_pose(struct xrt_device *xdev,
                        enum xrt_input_name name,
                        uint64_t at_timestamp_ns,
                        struct xrt_space_relation *out_relation)
{
	struct ql_hmd *hmd = (struct ql_hmd *)(xdev);
	struct ql_xrsp_host *host = &hmd->sys->xrsp_host;

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		QUEST_LINK_ERROR("Unknown input name");
		return;
	}

	os_mutex_lock(&host->pose_mutex);

	struct xrt_space_relation relation;
	U_ZERO(&relation);

	relation.pose = hmd->pose;
	relation.angular_velocity = hmd->angvel;
	relation.linear_velocity = hmd->vel;
	
	relation.relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_POSITION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

	timepoint_ns prediction_ns = at_timestamp_ns - hmd->pose_ns;
	double prediction_s = time_ns_to_s(prediction_ns);
	os_mutex_unlock(&host->pose_mutex);

	m_predict_relation(&relation, prediction_s, out_relation);
}

static void
ql_get_view_poses(struct xrt_device *xdev,
                      const struct xrt_vec3 *default_eye_relation,
                      uint64_t at_timestamp_ns,
                      uint32_t view_count,
                      struct xrt_space_relation *out_head_relation,
                      struct xrt_fov *out_fovs,
                      struct xrt_pose *out_poses)
{
	struct ql_hmd *hmd = (struct ql_hmd *)(xdev);
	struct ql_xrsp_host *host = &hmd->sys->xrsp_host;

	os_mutex_lock(&host->pose_mutex);

	struct xrt_vec3 modify_eye_relation = *default_eye_relation;
	modify_eye_relation.x = hmd->ipd_meters;
	//printf("%f\n", modify_eye_relation.x);

	//ql_hmd_get_interpolated_pose(hmd, at_timestamp_ns, NULL);

	os_mutex_unlock(&host->pose_mutex);
	u_device_get_view_poses(xdev, &modify_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
	                        out_poses);

	
}

static void
ql_hmd_destroy(struct xrt_device *xdev)
{
	struct ql_hmd *hmd = (struct ql_hmd *)(xdev);

	DRV_TRACE_MARKER();

	/* Remove this device from the system */
	ql_system_remove_hmd(hmd->sys);

	/* Drop the reference to the system */
	ql_system_reference(&hmd->sys, NULL);

	u_var_remove_root(hmd);

	u_device_free(&hmd->base);
}

static double foveate(double a, double b, double scale, double c, double x)
{
	// In order to save encoding, transmit and decoding time, only a portion of the image is encoded in full resolution.
	// on each axis, foveated coordinates are defined by the following formula.
	return a * tan(scale / a * (x - c)) + b;
	// a and b are defined such as:
	// edges of the image are not moved
	// f(-1) = -1
	// f( 1) =  1
	// the function also enforces pixel ratio 1:1 at fovea
	// df(x)/dx = scale for x = c
}

static std::tuple<float, float> solve_foveation(float scale, float c)
{
	// Compute a and b for the foveation function such that:
	//   foveate(a, b, scale, c, -1) = -1   (eq. 1)
	//   foveate(a, b, scale, c,  1) =  1   (eq. 2)
	//
	// The first step is to solve for a by subtracting equation 1 and 2:
	//   foveate(a, b, scale, c, 1) - foveate(a, b, scale, c, -1) = 2  (eq. 3)
	//
	// Where b is cancelled by the subtraction, so the equation to solve becomes:
	// f(a) = 0 where:
	auto f = [scale, c](double a) { return foveate(a, 0, scale, c, 1) - foveate(a, 0, scale, c, -1) - 2; };

	// b is computed rewriting equation 2 as:
	//   foveate(a, 0, scale, c,  1) + b = 1
	// Therefore:
	//   b = 1 - foveate(a, 0, scale, c,  1)
	//
	// Note that there are infinitely many solutions to equation 3, but we want
	// to have a value of a such that:
	//   ∀ x ∈ [-1, 1], abs(scale / a * (x - c)) < π / 2  (eq. 4)
	// So that foveate(x) is defined over [-1, 1]
	//
	// Equation 4 can be rewritten as:
	//   a > 2 * scale / π * abs(x - c)
	//
	// The minimum value of abs(x - c) for x ∈ [-1, 1] is 1 + abs(c)
	// so a must be larger than a0 with:
	double a0 = 2 * scale / M_PI * (1 + std::abs(c));

	// f is monotonically decreasing over (a0, +∞) with:
	//   lim   f(a) = +∞
	//   a→a0+
	//
	//   lim   f(a) = 2 * scale - 2
	//   a→∞
	//
	// Therefore there is one solution iff scale < 1
	//
	// a0 is the lowermost value for a, f(a0) is undefined and f(a0 + ε) > 0
	// We want an upper bound a1 for a, f(a1) < 0:
	//
	// Find the value by computing f(a0*2^n) until negative
	double a1 = a0 * 2;
	while (f(a1) > 0)
		a1 *= 2;

	// Solve f(a) = 0

	// last computed values for f(a0) and f(a1)
	std::optional<double> f_a0;
	double f_a1 = f(a1);

	int n = 0;
	double a;
	while (std::abs(a1 - a0) > 0.0000001 && n++ < 100)
	{
		if (not f_a0)
		{
			// use binary search
			a = 0.5 * (a0 + a1);
			double val = f(a);
			if (val > 0)
			{
				a0 = a;
				f_a0 = val;
			}
			else
			{
				a1 = a;
				f_a1 = val;
			}
		}
		else
		{
			// f(a1) is always defined
			// when f(a0) is defined, use secant method
			a = a1 - f_a1 * (a1 - a0) / (f_a1 - *f_a0);
			a0 = a1;
			a1 = a;
			f_a0 = f_a1;
			f_a1 = f(a);
		}
	}

	double b = 1 - foveate(a, 0, scale, c, 1);

	return {a, b};
}

bool ql_hmd_compute_distortion(xrt_device * xdev, uint32_t view_index, float u, float v, xrt_uv_triplet * result)
{
	// u,v are in the output coordinates (sent to the encoder)
	// result is in the input coordinates (from the application)
	const auto & param = ((ql_hmd *)xdev)->foveation_parameters[view_index];
	xrt_vec2 out;

	if (param.x.scale < 1)
	{
		u = 2 * u - 1;

		out.x = param.x.a * tan(param.x.scale / param.x.a * (u - param.x.center)) + param.x.b;
		out.x = std::clamp<float>((1 + out.x) / 2, 0, 1);
	}
	else
	{
		out.x = u;
	}

	if (param.y.scale < 1)
	{
		v = 2 * v - 1;

		out.y = param.y.a * tan(param.y.scale / param.y.a * (v - param.y.center)) + param.y.b;
		out.y = std::clamp<float>((1 + out.y) / 2, 0, 1);
	}
	else
	{
		out.y = v;
	}

	result->r = out;
	result->g = out;
	result->b = out;

	return true;
}

void ql_hmd_compute_undistortion(xrt_device * xdev, int view_index, float u, float v, xrt_vec2* out)
{
	const auto & param = ((ql_hmd *)xdev)->foveation_parameters[view_index];

	if (param.x.scale < 1)
	{
		out->x = param.x.center + (param.x.a / param.x.scale) * atan((u - param.x.b) / param.x.a);
		out->x = std::clamp<float>((1 + out->x) / 2, 0, 1);
	}
	else
	{
		out->x = u;
	}

	if (param.y.scale < 1)
	{
		out->y = param.y.center + (param.y.a / param.y.scale) * atan((v - param.y.b) / param.y.a);
		out->y = std::clamp<float>((1 + out->y) / 2, 0, 1);
	}
	else
	{
		out->y = v;
	}
}



static int
index_for(int row, int col, uint32_t stride, uint32_t offset)
{
	return row * stride + col + offset;
}

void ql_hmd_set_per_eye_resolution(struct ql_hmd* hmd, uint32_t w, uint32_t h, float fps)
{
	// Align up to macroblocks
	if (w % 16 != 0) {
		w += (w % 16);
	}
	if (h % 16 != 0) {
		h += (h % 16);
	}

	auto eye_width = w/2;
	auto eye_height = h;

	// Setup info.
	hmd->base.hmd->blend_modes[0] = XRT_BLEND_MODE_OPAQUE;
	hmd->base.hmd->blend_mode_count = 1;
	hmd->base.hmd->distortion.models = XRT_DISTORTION_MODEL_COMPUTE;
	hmd->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_COMPUTE;

	hmd->base.hmd->screens[0].w_pixels = eye_width * 2;
	hmd->base.hmd->screens[0].h_pixels = eye_height;
	hmd->base.hmd->screens[0].nominal_frame_interval_ns = 1000000000 / (fps); // HACK

	// Left
	hmd->base.hmd->views[0].display.w_pixels = eye_width;
	hmd->base.hmd->views[0].display.h_pixels = eye_height;
	hmd->base.hmd->views[0].viewport.x_pixels = 0;
	hmd->base.hmd->views[0].viewport.y_pixels = 0;
	hmd->base.hmd->views[0].viewport.w_pixels = eye_width;
	hmd->base.hmd->views[0].viewport.h_pixels = eye_height;
	hmd->base.hmd->views[0].rot = u_device_rotation_ident;

	// Right
	hmd->base.hmd->views[1].display.w_pixels = eye_width;
	hmd->base.hmd->views[1].display.h_pixels = eye_height;
	hmd->base.hmd->views[1].viewport.x_pixels = eye_width;
	hmd->base.hmd->views[1].viewport.y_pixels = 0;
	hmd->base.hmd->views[1].viewport.w_pixels = eye_width;
	hmd->base.hmd->views[1].viewport.h_pixels = eye_height;
	hmd->base.hmd->views[1].rot = u_device_rotation_ident;

	hmd->encode_width = eye_width * 2;
	hmd->encode_height = eye_height;
	hmd->fps = fps;

	float scale[] = {0.75, 0.75};

	for (int i = 0; i < 2; ++i)
	{
		hmd->foveation_parameters[i].x.scale = scale[0];
		hmd->foveation_parameters[i].y.scale = scale[1];
		const auto & fov = hmd->base.hmd->distortion.fov[i];
		float l = tan(fov.angle_left);
		float r = tan(fov.angle_right);
		float t = tan(fov.angle_up);
		float b = tan(fov.angle_down);
		if (scale[0] < 1)
		{
			float cu = (r + l) / (l - r);
			hmd->foveation_parameters[i].x.center = cu;

			std::tie(hmd->foveation_parameters[i].x.a, hmd->foveation_parameters[i].x.b) = solve_foveation(scale[0], cu);
		}

		if (scale[1] < 1)
		{
			float cv = (t + b) / (t - b);
			hmd->foveation_parameters[i].y.center = cv;

			std::tie(hmd->foveation_parameters[i].y.a, hmd->foveation_parameters[i].y.b) = solve_foveation(scale[1], cv);
		}
	}

	// Fill in distortion information
	hmd->base.compute_distortion = ql_hmd_compute_distortion;
	u_distortion_mesh_fill_in_compute(&hmd->base);


	// Quest Link expects undistortion information, so we have
	// to go through and convert everything to what it wants.
	if (hmd->quest_vertices) {
		free(hmd->quest_vertices);
	}
	if (hmd->quest_indices) {
		free(hmd->quest_indices);
	}

	uint32_t num = (uint32_t)debug_get_num_option_mesh_size();
	uint32_t cells_cols = num;
	uint32_t cells_rows = num;
	uint32_t vert_cols = cells_cols + 1;
	uint32_t vert_rows = cells_rows + 1;

	hmd->quest_vtx_count = hmd->base.hmd->distortion.mesh.vertex_count;
	hmd->quest_vertices = (float*)malloc(hmd->quest_vtx_count * sizeof(float) * 4);

	for (int i = 0; i < hmd->quest_vtx_count; i++)
	{
		float* vtx_dat = hmd->base.hmd->distortion.mesh.vertices + ((hmd->base.hmd->distortion.mesh.stride / sizeof(float)) * i);
		float* vtx_out = hmd->quest_vertices + (4 * i);

		xrt_vec2 undist = {0., 0.};
		ql_hmd_compute_undistortion(&hmd->base, (i >= hmd->quest_vtx_count/2) ? 1 : 0, vtx_dat[0], vtx_dat[1], &undist);

		float v1 = -vtx_dat[1];
		float v2 = undist.y;

		float u1 = (vtx_dat[0] - 1.0) / 2.0;
		float u2 = undist.x / 2.0;

		if (i >= hmd->quest_vtx_count/2) {
			u2 += 0.5;
		}

		if ((i % vert_cols) >= vert_cols-1)
		{
			//u2 = 0.0;
			//printf("%u: %f %f, %f %f\n", i, u1, v1, u2, v2);
		}
		//u1 = 0.0;
		//v1 = 0.0;
		
		vtx_out[0] = u1;
		vtx_out[1] = v1;
		vtx_out[2] = u2;
		vtx_out[3] = v2;
	}

	

	const int num_views = 2;
	const int tris_per_cell = 2;
	hmd->quest_index_count = cells_cols * cells_rows * 3 * num_views * tris_per_cell /* tris per cell */;
	hmd->quest_indices = (int16_t*)malloc(hmd->quest_index_count * sizeof(int16_t));

	// Set up indices, Quest Link uses tris instead of triangle strips.
	int idx = 0;
	for (int view = 0; view < 2; view++) {
		uint32_t off = view * (hmd->quest_vtx_count / 2);
		for (uint32_t r = 0; r < cells_rows; r++) {
			for (uint32_t c = 0; c < cells_cols; c++) {
				hmd->quest_indices[idx++] = index_for(r, c, vert_cols, off);
				hmd->quest_indices[idx++] = index_for(r, c + 1, vert_cols, off);
				hmd->quest_indices[idx++] = index_for(r + 1, c, vert_cols, off);

				hmd->quest_indices[idx++] = index_for(r, c + 1, vert_cols, off);
				hmd->quest_indices[idx++] = index_for(r + 1, c, vert_cols, off);
				hmd->quest_indices[idx++] = index_for(r + 1, c + 1, vert_cols, off);
			}
		}
	}
}

struct ql_hmd *
ql_hmd_create(struct ql_system *sys, const unsigned char *hmd_serial_no, struct ql_hmd_config *config)
{
	DRV_TRACE_MARKER();

	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);

	struct ql_hmd *hmd = U_DEVICE_ALLOCATE(struct ql_hmd, flags, 1, 0);
	if (hmd == NULL) {
		return NULL;
	}

	/* Take a reference to the ql_system */
	ql_system_reference(&hmd->sys, sys);

	hmd->config = config;

	hmd->base.tracking_origin = &sys->base;

	hmd->base.update_inputs = ql_update_inputs;
	hmd->base.get_tracked_pose = ql_get_tracked_pose;
	hmd->base.get_view_poses = ql_get_view_poses;
	hmd->base.create_compositor_target = ql_hmd_create_compositor_target;
	hmd->base.destroy = ql_hmd_destroy;
	hmd->base.name = XRT_DEVICE_GENERIC_HMD;
	hmd->base.device_type = XRT_DEVICE_TYPE_HMD;

	//hmd->tracker = ql_system_get_tracker(sys);

	// Print name.
	snprintf(hmd->base.str, XRT_DEVICE_NAME_LEN, "Meta Quest Link");
	snprintf(hmd->base.serial, XRT_DEVICE_NAME_LEN, "%s", hmd_serial_no);

	// Setup input.
	hmd->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	hmd->created_ns = os_monotonic_get_ns();
	hmd->pose_ns = hmd->created_ns;

	hmd->pose.position = {0.0f, 0.0f, 0.0f};
	hmd->pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};

	hmd->vel = {0.0f, 0.0f, 0.0f};
	hmd->acc = {0.0f, 0.0f, 0.0f};
	hmd->angvel = {0.0f, 0.0f, 0.0f};
	hmd->angacc = {0.0f, 0.0f, 0.0f};

	auto eye_width = 3616/2;
	auto eye_height = 1920;
	
	// Default FOV from Oculus Quest
	hmd->base.hmd->distortion.fov[0].angle_up = 48 * M_PI / 180;
	hmd->base.hmd->distortion.fov[0].angle_down = -50 * M_PI / 180;
	hmd->base.hmd->distortion.fov[0].angle_left = -52 * M_PI / 180;
	hmd->base.hmd->distortion.fov[0].angle_right = 45 * M_PI / 180;
	
	hmd->base.hmd->distortion.fov[1].angle_up = 48 * M_PI / 180;
	hmd->base.hmd->distortion.fov[1].angle_down = -50 * M_PI / 180;
	hmd->base.hmd->distortion.fov[1].angle_left = -45 * M_PI / 180;
	hmd->base.hmd->distortion.fov[1].angle_right = 52 * M_PI / 180;

	hmd->ipd_meters = 0.063;

	ql_hmd_set_per_eye_resolution(hmd, eye_width, eye_height, 10.0);

	const float min_cutoff = M_PI; //!< Default minimum cutoff frequency
	const float min_dcutoff = 1;   //!< Default minimum cutoff frequency for the derivative
	const float beta = 0.16;       //!< Default speed coefficient

	m_filter_euro_quat_init(&hmd->eye_l_oe, min_cutoff, min_dcutoff, beta);
	m_filter_euro_quat_init(&hmd->eye_r_oe, min_cutoff, min_dcutoff, beta);


#if 0
	// Setup info.
	struct u_device_simple_info info;
	info.display.w_pixels = 3616;
	info.display.h_pixels = 1920;
	info.display.w_meters = 0.13f;
	info.display.h_meters = 0.07f;
	info.lens_horizontal_separation_meters = 0.13f / 2.0f;
	info.lens_vertical_position_meters = 0.07f / 2.0f;
	info.fov[0] = 85.0f * ((float)(M_PI) / 180.0f);
	info.fov[1] = 85.0f * ((float)(M_PI) / 180.0f);

	if (!u_device_setup_split_side_by_side(&hmd->base, &info)) {
		QUEST_LINK_ERROR("Failed to setup basic device info");
		ql_hmd_destroy(&hmd->base);
		return NULL;
	}
#endif

	//u_distortion_mesh_set_none(&hmd->base);
	

	u_var_add_gui_header(hmd, NULL, "Misc");
	//u_var_add_log_level(hmd, &ql_log_level, "log_level");

	QUEST_LINK_DEBUG("Meta Quest Link HMD serial %s initialised.", hmd_serial_no);

	return hmd;

cleanup:
	ql_system_reference(&hmd->sys, NULL);
	return NULL;
}

/*
 *
 * Factory
 *
 */

static struct comp_target* hack_comp_target = NULL; // HACK

static bool
detect(const struct comp_target_factory *ctf, struct comp_compositor *c)
{
	return true;
}

static bool
create_target(const struct comp_target_factory *ctf, struct comp_compositor *c, struct comp_target **out_ct)
{
	struct comp_target *ct = hack_comp_target;//comp_window_none_create(c);
	if (ct == NULL) {
		return false;
	}

	*out_ct = ct;

	return true;
}

const struct comp_target_factory comp_target_factory_ql = {
    .name = "Quest Link Compositor",
    .identifier = "ql_comp",
    .requires_vulkan_for_create = true,
    .is_deferred = false,
    .required_instance_extensions = NULL,
    .required_instance_extension_count = 0,
    .detect = detect,
    .create_target = create_target,
};

static void ql_hmd_create_compositor_target(struct xrt_device * xdev,
                                               struct comp_compositor * comp,
                                               const struct comp_target_factory ** out_target)
{
	struct ql_hmd *hmd = (struct ql_hmd *)(xdev);
	while (!hmd->sys->xrsp_host.ready_to_send_frames) {
		os_nanosleep(U_TIME_1MS_IN_NS * 10);
	}

	hack_comp_target = comp_target_ql_create(&hmd->sys->xrsp_host, hmd->fps);
	hack_comp_target->c = comp;

	*out_target = &comp_target_factory_ql;
}