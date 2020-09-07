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
struct ns_optical_system;

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
struct ns_v1_eye
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

struct ns_v2_eye
{
	float x_coefficients[16];
	float y_coefficients[16];
	struct xrt_pose eye_pose;
	struct xrt_fov fov;
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
	struct xrt_pose pose;

	const char *config_path;

	struct ns_v1_eye eye_configs_v1[2]; // will be NULL if is_v2.
	struct ns_v2_eye eye_configs_v2[2]; // will be NULL if !is_v2

	struct ns_leap leap_config; // will be NULL if is_v2

	struct xrt_device *tracker;

	bool print_spew;
	bool print_debug;
	bool is_v2; // True if V2, false if V1. If we ever get a v3 this should
	            // be an enum or something
};


/*!
 * The mesh generator for the North Star distortion.
 *
 * @ingroup drv_ns
 * @implements u_uv_generator
 */
struct ns_mesh
{
	struct u_uv_generator base;
	struct ns_hmd *ns;
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
 * Get the North Star mesh generator from a @ref u_uv_generator.
 *
 * @ingroup drv_ns
 */
static inline struct ns_mesh *
ns_mesh(struct u_uv_generator *gen)
{
	return (struct ns_mesh *)gen;
}

/*!
 * Convert the display UV to the render UV using the distortion mesh.
 *
 * @ingroup drv_ns
 */
void
ns_display_uv_to_render_uv(struct ns_uv in,
                           struct ns_uv *out,
                           struct ns_v1_eye *eye);

struct ns_optical_system *
ns_create_optical_system(struct ns_v1_eye *eye);


#ifdef __cplusplus
}
#endif
