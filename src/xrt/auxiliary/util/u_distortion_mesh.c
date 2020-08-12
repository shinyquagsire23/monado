// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to generate disortion meshes.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_misc.h"
#include "util/u_frame.h"
#include "util/u_debug.h"
#include "util/u_format.h"
#include "util/u_distortion_mesh.h"

#include "math/m_vec2.h"

#include <stdio.h>
#include <assert.h>


DEBUG_GET_ONCE_NUM_OPTION(mesh_size, "XRT_MESH_SIZE", 64)


typedef bool (*func_calc)(struct xrt_device *xdev,
                          int view,
                          float u,
                          float v,
                          struct xrt_vec2_triplet *result);

static int
index_for(int row, int col, int stride, int offset)
{
	return row * stride + col + offset;
}

void
run_func(struct xrt_device *xdev,
         func_calc calc,
         int num_views,
         struct xrt_hmd_parts *target,
         size_t num)
{
	assert(calc != NULL);
	assert(num_views == 2);
	assert(num_views <= 2);

	size_t offset_vertices[2] = {0};
	size_t offset_indices[2] = {0};

	int cells_cols = num;
	int cells_rows = num;
	int vert_cols = cells_cols + 1;
	int vert_rows = cells_rows + 1;

	size_t num_vertices_per_view = vert_rows * vert_cols;
	size_t num_vertices = num_vertices_per_view * num_views;

	size_t num_uv_channels = 3;
	size_t stride_in_floats = 2 + num_uv_channels * 2;
	size_t num_floats = num_vertices * stride_in_floats;

	float *verts = U_TYPED_ARRAY_CALLOC(float, num_floats);

	// Setup the vertices for all views.
	size_t i = 0;
	for (int view = 0; view < num_views; view++) {
		offset_vertices[view] = i / stride_in_floats;

		for (int r = 0; r < vert_rows; r++) {
			// This goes from 0 to 1.0 inclusive.
			float v = (float)r / (float)cells_rows;

			for (int c = 0; c < vert_cols; c++) {
				// This goes from 0 to 1.0 inclusive.
				float u = (float)c / (float)cells_cols;

				// Make the position in the range of [-1, 1]
				verts[i + 0] = u * 2.0 - 1.0;
				verts[i + 1] = v * 2.0 - 1.0;

				if (!calc(xdev, view, u, v,
				          (struct xrt_vec2_triplet
				               *)&verts[i + 2])) {
					// bail on error, without updating
					// distortion.preferred
					return;
				}

				i += stride_in_floats;
			}
		}
	}

	size_t num_indices_per_view = cells_rows * (vert_cols * 2 + 2);
	size_t num_indices = num_indices_per_view * num_views;
	int *indices = U_TYPED_ARRAY_CALLOC(int, num_indices);

	// Set up indices for all views.
	i = 0;
	for (int view = 0; view < num_views; view++) {
		offset_indices[view] = i;

		size_t off = offset_vertices[view];

		for (int r = 0; r < cells_rows; r++) {
			// Top vertex row for this cell row, left most vertex.
			indices[i++] = index_for(r, 0, vert_cols, off);

			for (int c = 0; c < vert_cols; c++) {
				indices[i++] = index_for(r, c, vert_cols, off);
				indices[i++] =
				    index_for(r + 1, c, vert_cols, off);
			}

			// Bottom vertex row for this cell row, right most
			// vertex.
			indices[i++] =
			    index_for(r + 1, vert_cols - 1, vert_cols, off);
		}
	}

	target->distortion.models = XRT_DISTORTION_MODEL_MESHUV;
	target->distortion.preferred = XRT_DISTORTION_MODEL_MESHUV;
	target->distortion.mesh.vertices = verts;
	target->distortion.mesh.stride = stride_in_floats * sizeof(float);
	target->distortion.mesh.num_vertices = num_vertices;
	target->distortion.mesh.num_uv_channels = num_uv_channels;
	target->distortion.mesh.indices = indices;
	target->distortion.mesh.num_indices[0] = num_indices_per_view;
	target->distortion.mesh.num_indices[1] = num_indices_per_view;
	target->distortion.mesh.offset_indices[0] = offset_indices[0];
	target->distortion.mesh.offset_indices[1] = offset_indices[1];
	target->distortion.mesh.total_num_indices = num_indices;
}

bool
u_compute_distortion_vive(float aspect_x_over_y,
                          float grow_for_undistort,
                          float undistort_r2_cutoff,
                          float center[2],
                          float coefficients[3][3],
                          float u,
                          float v,
                          struct xrt_vec2_triplet *result)
{
	struct xrt_vec2 factor = {0.5 / (1.0 + grow_for_undistort),
	                          aspect_x_over_y * 0.5 /
	                              (1.0 + grow_for_undistort)};

	struct xrt_vec2 texCoord = {2.0 * u - 1.0, 2.0 * v - 1.0};

	texCoord.y /= aspect_x_over_y;
	texCoord.x -= center[0];
	texCoord.y -= center[1];

	float r2 = m_vec2_dot(texCoord, texCoord);


	struct xrt_vec3 d_inv = {
	    (r2 * coefficients[2][0] + coefficients[1][0]) * r2 +
	        coefficients[0][0] * r2 + 1.0,
	    (r2 * coefficients[2][1] + coefficients[1][1]) * r2 +
	        coefficients[0][1] * r2 + 1.0,
	    (r2 * coefficients[2][2] + coefficients[1][2]) * r2 +
	        coefficients[0][2] * r2 + 1.0};

	struct xrt_vec3 d = {1.0 / d_inv.x, 1.0 / d_inv.y, 1.0 / d_inv.z};

	struct xrt_vec2 offset = {0.5, 0.5};

	struct xrt_vec2 tc_r = {
	    offset.x + (texCoord.x * d.x + center[0]) * factor.x,
	    offset.y + (texCoord.y * d.x + center[1]) * factor.y};

	struct xrt_vec2 tc_g = {
	    offset.x + (texCoord.x * d.y + center[0]) * factor.x,
	    offset.y + (texCoord.y * d.y + center[1]) * factor.y};
	struct xrt_vec2 tc_b = {
	    offset.x + (texCoord.x * d.z + center[0]) * factor.x,
	    offset.y + (texCoord.y * d.z + center[1]) * factor.y};

	result->r = tc_r;
	result->g = tc_g;
	result->b = tc_b;
	return true;
}

#define mul m_vec2_mul
#define mul_scalar m_vec2_mul_scalar
#define add m_vec2_add
#define sub m_vec2_sub
#define div m_vec2_div
#define div_scalar m_vec2_div_scalar
#define len m_vec2_len

bool
u_compute_distortion_panotools(struct u_panotools_values *values,
                               float u,
                               float v,
                               struct xrt_vec2_triplet *result)
{
	const struct u_panotools_values val = *values;

	struct xrt_vec2 r = {u, v};
	r = mul(r, val.viewport_size);
	r = sub(r, val.lens_center);
	r = div_scalar(r, val.scale);

	float r_mag = len(r);
	r_mag = val.distortion_k[0] +                                // r^1
	        val.distortion_k[1] * r_mag +                        // r^2
	        val.distortion_k[2] * r_mag * r_mag +                // r^3
	        val.distortion_k[3] * r_mag * r_mag * r_mag +        // r^4
	        val.distortion_k[4] * r_mag * r_mag * r_mag * r_mag; // r^5

	struct xrt_vec2 r_dist = mul_scalar(r, r_mag);
	r_dist = mul_scalar(r_dist, val.scale);

	struct xrt_vec2 r_uv = mul_scalar(r_dist, val.aberration_k[0]);
	r_uv = add(r_uv, val.lens_center);
	r_uv = div(r_uv, val.viewport_size);

	struct xrt_vec2 g_uv = mul_scalar(r_dist, val.aberration_k[1]);
	g_uv = add(g_uv, val.lens_center);
	g_uv = div(g_uv, val.viewport_size);

	struct xrt_vec2 b_uv = mul_scalar(r_dist, val.aberration_k[2]);
	b_uv = add(b_uv, val.lens_center);
	b_uv = div(b_uv, val.viewport_size);

	result->r = r_uv;
	result->g = g_uv;
	result->b = b_uv;
	return true;
}

bool
u_compute_distortion_none(float u, float v, struct xrt_vec2_triplet *result)
{
	result->r.x = u;
	result->r.y = v;
	result->g.x = u;
	result->g.y = v;
	result->b.x = u;
	result->b.y = v;
	return true;
}

static bool
compute_distortion_none(struct xrt_device *xdev,
                        int view,
                        float u,
                        float v,
                        struct xrt_vec2_triplet *result)
{
	return u_compute_distortion_none(u, v, result);
}

void
u_compute_distortion_mesh(struct xrt_device *xdev)
{
	struct xrt_hmd_parts *target = xdev->hmd;
	func_calc calc = xdev->compute_distortion;
	if (calc == NULL) {
		calc = compute_distortion_none;
	}
	size_t num = debug_get_num_option_mesh_size();
	run_func(xdev, calc, 2, target, num);
}
