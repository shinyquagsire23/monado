// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main compositor written using Vulkan header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp
 */

#pragma once

#include "main/comp_settings.h"
#include "main/comp_window.h"
#include "main/comp_renderer.h"

#include "xrt/xrt_gfx_vk.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Structs
 *
 */

/*!
 * A single swapchain image, holds the needed state for tracking image usage.
 *
 * @ingroup comp
 */
struct comp_swapchain_image
{
	//! Vulkan image to create view from.
	VkImage image;
	//! Exported memory backing the image.
	VkDeviceMemory memory;
	//! Sampler used by the renderer and distortion code.
	VkSampler sampler;
	//! Views used by the renderer and distortion code, for each array layer.
	VkImageView *views;
};

/*!
 * A swapchain that is almost a one to one mapping to a OpenXR swapchain.
 *
 * Not used by the window backend that uses the vk_swapchain to render to.
 *
 * @ingroup comp
 */
struct comp_swapchain
{
	struct xrt_swapchain_fd base;

	struct comp_compositor *c;

	struct comp_swapchain_image images[XRT_MAX_SWAPCHAIN_IMAGES];
};

/*!
 * Main compositor struct tying everything in the compositor together.
 *
 * @ingroup comp
 */
struct comp_compositor
{
	struct xrt_compositor_fd base;

	//! A link back to the compositor we are presenting to the client.
	struct xrt_compositor *client;

	//! Renderer helper.
	struct comp_renderer *r;

	//! The window or display we are using.
	struct comp_window *window;

	//! The device we are displaying to.
	struct xrt_device *xdev;

	//! The timekeeping state object.
	struct time_state *timekeeping;

	//! The settings.
	struct comp_settings settings;

	//! Vulkan bundle of things.
	struct vk_bundle vk;

	//! Timestamp of last-rendered (immersive) frame.
	int64_t last_frame_time_ns;

	/*!
	 * The current state we are tracking.
	 *
	 * Settings is supposed to be read only.
	 */
	struct
	{
		uint32_t width;
		uint32_t height;
	} current;
};


/*
 *
 * Functions and helpers.
 *
 */

/*!
 * Convinence function to convert a xrt_swapchain to a comp_swapchain.
 *
 * @ingroup comp
 */
XRT_MAYBE_UNUSED static struct comp_swapchain *
comp_swapchain(struct xrt_swapchain *xsc)
{
	return (struct comp_swapchain *)xsc;
}

/*!
 * Convinence function to convert a xrt_compositor to a comp_compositor.
 *
 * @ingroup comp
 */
XRT_MAYBE_UNUSED static struct comp_compositor *
comp_compositor(struct xrt_compositor *xc)
{
	return (struct comp_compositor *)xc;
}

/*!
 * A compositor function that is implemented in the swapchain code.
 *
 * @ingroup comp
 */
struct xrt_swapchain *
comp_swapchain_create(struct xrt_compositor *xc,
                      enum xrt_swapchain_create_flags create,
                      enum xrt_swapchain_usage_bits bits,
                      int64_t format,
                      uint32_t sample_count,
                      uint32_t width,
                      uint32_t height,
                      uint32_t face_count,
                      uint32_t array_size,
                      uint32_t mip_count);

/*!
 * Free and destroy any initialized fields on the given image, safe to pass in
 * images that has one or all fields set to NULL.
 *
 * @ingroup comp
 */
void
comp_swapchain_image_cleanup(struct vk_bundle *vk,
			     uint32_t array_size,
                             struct comp_swapchain_image *image);

/*!
 * Printer helper.
 *
 * @ingroup comp
 */
void
comp_compositor_print(struct comp_compositor *c,
                      const char *func,
                      const char *fmt,
                      ...) XRT_PRINTF_FORMAT(3, 4);

/*!
 * Spew level logging.
 *
 * @ingroup comp
 */
#define COMP_SPEW(c, ...)                                                      \
	do {                                                                   \
		if (c->settings.print_spew) {                                  \
			comp_compositor_print(c, __func__, __VA_ARGS__);       \
		}                                                              \
	} while (false)

/*!
 * Debug level logging.
 *
 * @ingroup comp
 */
#define COMP_DEBUG(c, ...)                                                     \
	do {                                                                   \
		if (c->settings.print_debug) {                                 \
			comp_compositor_print(c, __func__, __VA_ARGS__);       \
		}                                                              \
	} while (false)

/*!
 * Error level logging.
 *
 * @ingroup comp
 */
#define COMP_ERROR(c, ...)                                                     \
	do {                                                                   \
		comp_compositor_print(c, __func__, __VA_ARGS__);               \
	} while (false)


#ifdef __cplusplus
}
#endif
