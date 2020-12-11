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
#include "xrt/xrt_defines.h"

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
 * Reset renderer as input has changed.
 *
 * @ingroup comp_main
 */
void
comp_renderer_reset(struct comp_renderer *r);

/*!
 * Clean up and free the renderer.
 *
 * @ingroup comp_main
 */
void
comp_renderer_destroy(struct comp_renderer *r);

/*!
 * Render frame.
 *
 * @ingroup comp_main
 */
void
comp_renderer_draw(struct comp_renderer *r);

void
comp_renderer_set_projection_layer(struct comp_renderer *r,
                                   uint32_t layer,
                                   struct comp_swapchain_image *left_image,
                                   struct comp_swapchain_image *right_image,
                                   struct xrt_layer_data *data);

void
comp_renderer_set_quad_layer(struct comp_renderer *r,
                             uint32_t layer,
                             struct comp_swapchain_image *image,
                             struct xrt_layer_data *data);

void
comp_renderer_set_cylinder_layer(struct comp_renderer *r,
                                 uint32_t layer,
                                 struct comp_swapchain_image *image,
                                 struct xrt_layer_data *data);

#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT1
void
comp_renderer_set_equirect1_layer(struct comp_renderer *r,
                                  uint32_t layer,
                                  struct comp_swapchain_image *image,
                                  struct xrt_layer_data *data);
#endif
#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT2
void
comp_renderer_set_equirect2_layer(struct comp_renderer *r,
                                  uint32_t layer,
                                  struct comp_swapchain_image *image,
                                  struct xrt_layer_data *data);
#endif

void
comp_renderer_allocate_layers(struct comp_renderer *self, uint32_t num_layers);

void
comp_renderer_destroy_layers(struct comp_renderer *self);

#ifdef __cplusplus
}
#endif
