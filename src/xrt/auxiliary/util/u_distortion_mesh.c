// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  @ref xrt_frame helpers.
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


/*
 *
 * Func running helpers.
 *
 */

typedef void (*func_cb)(
    struct u_uv_generator *gen, int view, float x, float y, float result[6]);

static int
index_for(int row, int col, int stride, int offset)
{
	return row * stride + col + offset;
}

void
run_func(struct u_uv_generator *gen,
         int num_views,
         struct xrt_hmd_parts *target,
         size_t num)
{
	assert(gen != NULL);
	assert(num_views == 2);
	assert(num_views <= 2);

	func_cb func = gen->calc;

	size_t offset_vertices[2] = {0};
	size_t offset_indices[2] = {0};

	int cells_cols = num;
	int cells_rows = num;
	int vert_cols = cells_cols + 1;
	int vert_rows = cells_rows + 1;

	size_t num_vertices_per_view = vert_rows * vert_cols;
	size_t num_vertices = num_vertices_per_view * num_views;

	size_t stride_in_floats = 8;
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
				func(gen, view, u, v, &verts[i + 2]);

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
	target->distortion.mesh.num_uv_channels = 3;
	target->distortion.mesh.indices = indices;
	target->distortion.mesh.num_indices[0] = num_indices_per_view;
	target->distortion.mesh.num_indices[1] = num_indices_per_view;
	target->distortion.mesh.offset_indices[0] = offset_indices[0];
	target->distortion.mesh.offset_indices[1] = offset_indices[1];
	target->distortion.mesh.total_num_indices = num_indices;
}

void
u_distortion_mesh_from_gen(struct u_uv_generator *gen,
                           int num_views,
                           struct xrt_hmd_parts *target)
{
	size_t num = debug_get_num_option_mesh_size();
	run_func(gen, num_views, target, num);
}


/*
 *
 * Panotools.
 *
 */

#define mul m_vec2_mul
#define mul_scalar m_vec2_mul_scalar
#define add m_vec2_add
#define sub m_vec2_sub
#define div m_vec2_div
#define div_scalar m_vec2_div_scalar
#define len m_vec2_len


struct panotools_state
{
	struct u_uv_generator base;

	const struct u_panotools_values *vals[2];
};

static void
panotools_calc(struct u_uv_generator *generator,
               int view,
               float u,
               float v,
               float result[6])
{
	struct panotools_state *state = (struct panotools_state *)generator;
	const struct u_panotools_values val = *state->vals[view];

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

	result[0] = r_uv.x;
	result[1] = r_uv.y;
	result[2] = g_uv.x;
	result[3] = g_uv.y;
	result[4] = b_uv.x;
	result[5] = b_uv.y;
}

static void
panotools_destroy(struct u_uv_generator *generator)
{
	free(generator);
}

static void
panotools_fill_in(const struct u_panotools_values *left,
                  const struct u_panotools_values *right,
                  struct panotools_state *state)
{
	state->base.calc = panotools_calc;
	state->base.destroy = panotools_destroy;
	state->vals[0] = left;
	state->vals[1] = right;
}

void
u_distortion_mesh_from_panotools(const struct u_panotools_values *left,
                                 const struct u_panotools_values *right,
                                 struct xrt_hmd_parts *target)
{
	struct panotools_state state;
	panotools_fill_in(left, right, &state);

	size_t num = debug_get_num_option_mesh_size();
	run_func(&state.base, 2, target, num);
}

void
u_distortion_mesh_generator_from_panotools(
    const struct u_panotools_values *left,
    const struct u_panotools_values *right,
    struct u_uv_generator **out_gen)
{
	struct panotools_state *state = U_TYPED_CALLOC(struct panotools_state);
	panotools_fill_in(left, right, state);
	*out_gen = &state->base;
}


/*
 *
 * No distortion.
 *
 */

static void
no_distortion_calc(struct u_uv_generator *generator,
                   int view,
                   float u,
                   float v,
                   float result[6])
{
	result[0] = u;
	result[1] = v;
	result[2] = u;
	result[3] = v;
	result[4] = u;
	result[5] = v;
}

void
u_distortion_mesh_none(struct xrt_hmd_parts *target)
{
	struct u_uv_generator gen;
	gen.calc = no_distortion_calc;

	run_func(&gen, 2, target, 8);
}
