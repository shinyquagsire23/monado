// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  The compositor compute based rendering code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_render
 */

#include "math/m_api.h"
#include "math/m_matrix_4x4_f64.h"

#include "render/render_interface.h"


/*!
 * Create a simplified projection matrix for timewarp.
 */
static void
calc_projection(const struct xrt_fov *fov, struct xrt_matrix_4x4_f64 *result)
{
	const double tan_left = tan(fov->angle_left);
	const double tan_right = tan(fov->angle_right);

	const double tan_down = tan(fov->angle_down);
	const double tan_up = tan(fov->angle_up);

	const bool vulkan_projection_space_y = true;

	const double tan_width = tan_right - tan_left;
	const double tan_height = vulkan_projection_space_y  // Projection space y direction:
	                              ? (tan_down - tan_up)  // Vulkan Y down
	                              : (tan_up - tan_down); // OpenGL Y up

	const double near_plane = 0.5;
	const double far_plane = 1.5;

	const double a11 = 2 / tan_width;
	const double a22 = 2 / tan_height;

	const double a31 = (tan_right + tan_left) / tan_width;
	const double a32 = (tan_up + tan_down) / tan_height;

	const float a33 = (float)(-far_plane / (far_plane - near_plane));
	const float a43 = (float)(-(far_plane * near_plane) / (far_plane - near_plane));


#if 1
	// We skip a33 & a43 because we don't have depth.
	(void)a33;
	(void)a43;

	// clang-format off
	*result = (struct xrt_matrix_4x4_f64){
		{
			      a11,         0,  0,  0,
			        0,       a22,  0,  0,
			      a31,       a32, -1,  0,
			        0,         0,  0,  1,
		}
	};
	// clang-format on
#else
	// clang-format off
	*result = (struct xrt_matrix_4x4_f64) {
		.v = {
			a11, 0, 0, 0,
			0, a22, 0, 0,
			a31, a32, a33, -1,
			0, 0, a43, 0,
		}
	};
	// clang-format on
#endif
}

void
render_calc_time_warp_matrix(const struct xrt_pose *src_pose,
                             const struct xrt_fov *src_fov,
                             const struct xrt_pose *new_pose,
                             struct xrt_matrix_4x4 *matrix)
{
	// Src projection matrix.
	struct xrt_matrix_4x4_f64 src_proj;
	calc_projection(src_fov, &src_proj);

	// Src rotation matrix.
	struct xrt_matrix_4x4_f64 src_rot_inv;
	struct xrt_quat src_q = src_pose->orientation;
	m_mat4_f64_orientation(&src_q, &src_rot_inv); // This is a model matrix, a inverted view matrix.

	// New rotation matrix.
	struct xrt_matrix_4x4_f64 new_rot, new_rot_inv;
	struct xrt_quat new_q = new_pose->orientation;
	m_mat4_f64_orientation(&new_q, &new_rot_inv); // This is a model matrix, a inverted view matrix.
	m_mat4_f64_invert(&new_rot_inv, &new_rot);    // Invert to make it a view matrix.

	// Combine both rotation matricies to get difference.
	struct xrt_matrix_4x4_f64 delta_rot, delta_rot_inv;
	m_mat4_f64_multiply(&new_rot, &src_rot_inv, &delta_rot);
	m_mat4_f64_invert(&delta_rot, &delta_rot_inv);

	// Combine the source projection matrix and
	struct xrt_matrix_4x4_f64 result;
	m_mat4_f64_multiply(&src_proj, &delta_rot_inv, &result);

	// Convert from f64 to f32.
	for (int i = 0; i < 16; i++) {
		matrix->v[i] = (float)result.v[i];
	}
}
