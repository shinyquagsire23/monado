// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header for misc OpenGL ES code, not a complete graphics provider.
 * @author Simon Ser <contact@emersion.fr>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @ingroup xrt_iface
 */
void
xrt_gfx_gles_get_versions(struct xrt_api_requirements *ver);


#ifdef __cplusplus
}
#endif
