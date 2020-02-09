// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface between North Star distortion and HMD code.
 * @author Nova King <technobaboo@gmail.com>
 * @ingroup drv_ns
 */

#pragma once

#include "math/m_api.h"
#include "util/u_distortion_mesh.h"
#include "util/u_json.h"
#include "util/u_misc.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_ns North Star Driver
 * @ingroup drv
 *
 * @brief Driver for the North Star HMD.
 */

/*
 *
 * Defines
 *
 */

#define NS_SPEW(c, ...)                                                        \
	do {                                                                   \
		if (c->print_spew) {                                           \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define NS_DEBUG(c, ...)                                                       \
	do {                                                                   \
		if (c->print_debug) {                                          \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define NS_ERROR(c, ...)                                                       \
	do {                                                                   \
		fprintf(stderr, "%s - ", __func__);                            \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, "\n");                                         \
	} while (false)


/*
 *
 * Structs
 *
 */


/*!
 * Opaque struct for optical system C++ integration
 *
 * @ingroup drv_ns
 */
typedef struct _ns_optical_system ns_optical_system;


/*!
 * Simple UV struct.
 *
 * @ingroup drv_ns
 */
struct ns_uv
{
	float u;
	float v;
};


/*!
 * Configuration information about the LMC or Rigel sensor according to the
 * configuration file.
 *
 * @ingroup drv_ns
 */
struct ns_leap
{
	const char *name;
	const char *serial;
	struct xrt_pose pose;
};


/*!
 * Distortion information about an eye parsed from the configuration file.
 *
 * @ingroup drv_ns
 */
struct ns_eye
{
	float ellipse_minor_axis;
	float ellipse_major_axis;

	struct xrt_vec3 screen_forward;
	struct xrt_vec3 screen_position;

	struct xrt_pose eye_pose;

	struct xrt_quat camera_projection;

	struct xrt_matrix_4x4 sphere_to_world_space;
	struct xrt_matrix_4x4 world_to_screen_space;

	ns_optical_system *optical_system;
};

/*!
 * Information about the whole North Star headset.
 *
 * @ingroup drv_ns
 */
struct ns_hmd
{
	struct xrt_device base;
	struct xrt_pose pose;

	const char *config_path;

	struct ns_eye eye_configs[2];
	struct ns_leap leap_config;

	struct xrt_device *tracker;

	bool print_spew;
	bool print_debug;
};

/*!
 * The mesh generator for the North Star distortion.
 *
 * @ingroup drv_ns
 */
struct ns_mesh
{
	struct u_uv_generator base;
	struct ns_hmd *ns;
};

/*!
 * @dir drivers/north_star
 *
 * @brief @ref drv_ns files.
 */


/*
 *
 * Functions
 *
 */

/*!
 * Convert the display UV to the render UV using the distortion mesh.
 *
 * @ingroup drv_ns
 */
void
ns_display_uv_to_render_uv(struct ns_uv display_uv,
                           struct ns_uv *render_uv,
                           struct ns_eye eye);

/*!
 * Get the North Star HMD information from the xrt_device.
 *
 * @ingroup drv_ns
 */
struct ns_hmd *
get_ns_hmd(struct xrt_device *xdev);


ns_optical_system *
ns_create_optical_system(struct ns_eye *eye);


#ifdef __cplusplus
}
#endif
