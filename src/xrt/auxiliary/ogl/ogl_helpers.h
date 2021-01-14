// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common OpenGL code header.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_ogl
 */

#pragma once

#include "xrt/xrt_compositor.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Determine the texture target and the texture binding parameter to
 * save/restore for creation/use of an OpenGL texture from the given info.
 */
void
ogl_texture_target_for_swapchain_info(const struct xrt_swapchain_create_info *info,
                                      uint32_t *out_tex_target,
                                      uint32_t *out_tex_param_name);

#ifdef __cplusplus
}
#endif
