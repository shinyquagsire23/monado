// Copyright 2019, Collabora, Ltd.
// Copyright 2020, Nova King.
// Copyright 2021, Moses Turner.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface between North Star distortion and HMD code.
 * @author Nova King <technobaboo@gmail.com>
 * @author Moses Turner <mosesturner@protonmail.com>
 * @ingroup drv_ns
 */

#pragma once

#include "math/m_api.h"
#include "util/u_json.h"
#include "util/u_misc.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "util/u_logging.h"
#include "os/os_threading.h"
#include "util/u_distortion_mesh.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *
 * 3D distortion structs
 * Sometimes known as "v1", config file name is often "Calibration.json"
 *
 */

/*!
 * Opaque struct for optical system C++ integration
 *
 * @ingroup drv_ns
 */
struct ns_3d_optical_system;

/*!
 * Distortion information about an eye parsed from the configuration file.
 *
 * @ingroup drv_ns
 */
struct ns_3d_eye
{
	float ellipse_minor_axis;
	float ellipse_major_axis;

	struct xrt_vec3 screen_forward;
	struct xrt_vec3 screen_position;

	struct xrt_pose eye_pose;

	// This is more of a vec4 than a quat
	struct xrt_quat camera_projection;

	struct xrt_matrix_4x4 sphere_to_world_space;
	struct xrt_matrix_4x4 world_to_screen_space;

	struct ns_optical_system *optical_system;
};

struct ns_3d_values
{
	struct ns_3d_eye eyes[2];
};

enum ns_distortion_type
{
	NS_DISTORTION_TYPE_INVALID,
	NS_DISTORTION_TYPE_GEOMETRIC_3D,
	NS_DISTORTION_TYPE_POLYNOMIAL_2D,
	NS_DISTORTION_TYPE_MOSHI_MESHGRID,
};

// The config json data gets dumped in here.
// In general, `target_builder_north_star.c` sets up tracking, and `ns_hmd.c` sets up distortion/optics.
struct ns_optics_config
{
	struct xrt_pose head_pose_to_eye[2]; // left, right
	struct xrt_fov fov[2];               // left,right



	enum ns_distortion_type distortion_type;
	union {
		struct ns_3d_values dist_3d;
		struct u_ns_p2d_values dist_p2d;
		struct u_ns_meshgrid_values dist_meshgrid;
	};
};


/*!
 * Information about the whole North Star headset.
 *
 * @ingroup drv_ns
 * @implements xrt_device
 */

struct ns_hmd
{
	struct xrt_device base;
	struct xrt_space_relation no_tracker_relation;
	const cJSON *config_json;
	struct ns_optics_config config;

	enum u_logging_level log_level;
};

/*
 *
 * Functions
 *
 */

/*!
 * Get the North Star HMD information from a @ref xrt_device.
 *
 * @ingroup drv_ns
 */
static inline struct ns_hmd *
ns_hmd(struct xrt_device *xdev)
{
	return (struct ns_hmd *)xdev;
}


/*!
 * Convert the display UV to the render UV using the distortion mesh.
 *
 * @ingroup drv_ns
 */

void
ns_3d_display_uv_to_render_uv(struct xrt_vec2 in, struct xrt_vec2 *out, struct ns_3d_eye *eye);

struct ns_optical_system *
ns_3d_create_optical_system(struct ns_3d_eye *eye);

void
ns_3d_free_optical_system(struct ns_optical_system **system);


#ifdef __cplusplus
}
#endif
