// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to OpenGL ES client side code.
 * @author Simon Ser <contact@emersion.fr>
 * @ingroup comp_client
 */

#include "xrt/xrt_gfx_gles.h"


void
xrt_gfx_gles_get_versions(struct xrt_api_requirements *ver)
{
	ver->min_major = 2;
	ver->min_minor = 0;
	ver->min_patch = 0;
	ver->max_major = 3;
	ver->max_minor = 2;
	ver->max_patch = 1024 - 1;
}
