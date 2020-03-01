// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor rendering code header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "xrt/xrt_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif


struct comp_compositor;
struct comp_renderer;
struct comp_swapchain_image;

/*!
 * Called by the main compositor code to create the renderer.
 *
 * @ingroup comp_main
 */
struct comp_renderer *
comp_renderer_create(struct comp_compositor *c);

/*!
 * Render a distorted stereo frame.
 *
 * @ingroup comp_main
 */
void
comp_renderer_frame(struct comp_renderer *r,
                    struct comp_swapchain_image *left,
                    uint32_t left_layer,
                    struct comp_swapchain_image *right,
                    uint32_t right_layer);

/*!
 * Clean up and free the renderer.
 *
 * @ingroup comp_main
 */
void
comp_renderer_destroy(struct comp_renderer *r);


#ifdef __cplusplus
}
#endif
