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
 * Printing functions.
 *
 */

#define NS_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->log_level, __VA_ARGS__)
#define NS_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->log_level, __VA_ARGS__)
#define NS_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->log_level, __VA_ARGS__)
#define NS_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->log_level, __VA_ARGS__)
#define NS_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->log_level, __VA_ARGS__)

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
 * Configuration information about the LMC or Rigel sensor according to the
 * configuration file.
 *
 * @ingroup drv_ns
 */
struct ns_3d_leap
{
	char name[64];
	char serial[64];
	struct xrt_pose pose;
};

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

	struct xrt_quat camera_projection;

	struct xrt_matrix_4x4 sphere_to_world_space;
	struct xrt_matrix_4x4 world_to_screen_space;

	struct ns_optical_system *optical_system;
};

struct ns_3d_data
{
	struct ns_3d_eye eyes[2];
	struct ns_3d_leap leap;
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
	const char *config_path;
	struct cJSON *config_json;
	struct xrt_pose head_pose_to_eye[2]; // left, right

	void (*free_distortion_values)(struct ns_hmd *hmd);
	union {
		struct ns_3d_data dist_3d;
		struct u_ns_p2d_values dist_p2d;
		struct u_ns_meshgrid_values dist_meshgrid;
	};

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
 * Create a North Star hmd.
 *
 * @ingroup drv_ns
 */
struct xrt_device *
ns_hmd_create(const char *config_path);


/*!
 * Convert the display UV to the render UV using the distortion mesh.
 *
 * @ingroup drv_ns
 */

void
ns_3d_display_uv_to_render_uv(struct xrt_vec2 in, struct xrt_vec2 *out, struct ns_3d_eye *eye);

struct ns_optical_system *
ns_3d_create_optical_system(struct ns_3d_eye *eye);


#ifdef __cplusplus
}
#endif
