// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to OpenGL client side code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include "xrt/xrt_gfx_gl.h"


void
xrt_gfx_gl_get_versions(struct xrt_api_requirements *ver)
{
	ver->min_major = 4;
	ver->min_minor = 5;
	ver->min_patch = 0;
	ver->max_major = 4;
	ver->max_minor = 6;
	ver->max_patch = (1024 - 1);
}
