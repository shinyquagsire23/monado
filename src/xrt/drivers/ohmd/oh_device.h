// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to OpenHMD driver code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ohmd
 */

#pragma once

#include "math/m_api.h"
#include "xrt/xrt_device.h"

#include "util/u_distortion_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ohmd_context ohmd_context;
typedef struct ohmd_device ohmd_device;

/*!
 * @implements xrt_device
 */
struct oh_device
{
	struct xrt_device base;
	ohmd_context *ctx;
	ohmd_device *dev;

	bool skip_ang_vel;

	int64_t last_update;
	struct xrt_space_relation last_relation;

	bool print_spew;
	bool print_debug;
	bool enable_finite_difference;

	struct
	{
		struct
		{
			//! Panotools universal distortion k.
			float distortion_k[4];
			//! Panotools post distortion scale, <r, g, b, _>.
			float aberration_k[4];
			//! Panotools warp scale.
			float warp_scale;
		} openhmd;
		struct u_vive_values vive;
	} distortion;
};

static inline struct oh_device *
oh_device(struct xrt_device *xdev)
{
	return (struct oh_device *)xdev;
}

struct oh_device *
oh_device_create(ohmd_context *ctx,
                 ohmd_device *dev,
                 const char *prod,
                 bool print_spew,
                 bool print_debug);

#define OH_SPEW(c, ...)                                                        \
	do {                                                                   \
		if (c->print_spew) {                                           \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define OH_DEBUG(c, ...)                                                       \
	do {                                                                   \
		if (c->print_debug) {                                          \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define OH_ERROR(c, ...)                                                       \
	do {                                                                   \
		fprintf(stderr, "%s - ", __func__);                            \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, "\n");                                         \
	} while (false)


#ifdef __cplusplus
}
#endif
