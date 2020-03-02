// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main compositor written using Vulkan header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_main
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
 * @ingroup comp_main
 */
struct comp_swapchain_image
{
	//! Vulkan image to create view from.
	VkImage image;
	//! Exported memory backing the image.
	VkDeviceMemory memory;
	//! Sampler used by the renderer and distortion code.
	VkSampler sampler;
	//! Views used by the renderer and distortion code, for each array
	//! layer.
	VkImageView *views;
};

/*!
 * A swapchain that is almost a one to one mapping to a OpenXR swapchain.
 *
 * Not used by the window backend that uses the vk_swapchain to render to.
 *
 * @ingroup comp_main
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
 * @ingroup comp_main
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

	//! The settings.
	struct comp_settings settings;

	//! Vulkan bundle of things.
	struct vk_bundle vk;

	//! Timestamp of last-rendered (immersive) frame.
	int64_t last_frame_time_ns;

	/*!
	 * @brief Data exclusive to the begin_frame/end_frame for computing an
	 * estimate of the app's needs.
	 */
	struct
	{
		int64_t last_begin;
		int64_t last_end;
	} app_profiling;

	//! The time our compositor needs to do rendering
	int64_t frame_overhead_ns;
	/*!
	 * @brief Estimated rendering time per frame of the application.
	 *
	 * Set by the begin_frame/end_frame code.
	 *
	 * @todo make this atomic.
	 */
	int64_t expected_app_duration_ns;
	//! The last time we provided in the results of wait_frame
	int64_t last_next_display_time;

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
 * @ingroup comp_main
 */
XRT_MAYBE_UNUSED static struct comp_swapchain *
comp_swapchain(struct xrt_swapchain *xsc)
{
	return (struct comp_swapchain *)xsc;
}

/*!
 * Convinence function to convert a xrt_compositor to a comp_compositor.
 *
 * @ingroup comp_main
 */
XRT_MAYBE_UNUSED static struct comp_compositor *
comp_compositor(struct xrt_compositor *xc)
{
	return (struct comp_compositor *)xc;
}

/*!
 * A compositor function that is implemented in the swapchain code.
 *
 * @ingroup comp_main
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
 * @ingroup comp_main
 */
void
comp_swapchain_image_cleanup(struct vk_bundle *vk,
                             uint32_t array_size,
                             struct comp_swapchain_image *image);

/*!
 * Printer helper.
 *
 * @ingroup comp_main
 */
void
comp_compositor_print(struct comp_compositor *c,
                      const char *func,
                      const char *fmt,
                      ...) XRT_PRINTF_FORMAT(3, 4);

/*!
 * Spew level logging.
 *
 * @ingroup comp_main
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
 * @ingroup comp_main
 */
#define COMP_DEBUG(c, ...)                                                     \
	do {                                                                   \
		if (c->settings.print_debug) {                                 \
			comp_compositor_print(c, __func__, __VA_ARGS__);       \
		}                                                              \
	} while (false)

/*!
 * Mode printing.
 *
 * @ingroup comp_main
 */
#define COMP_PRINT_MODE(c, ...)                                                \
	do {                                                                   \
		if (c->settings.print_modes) {                                 \
			comp_compositor_print(c, __func__, __VA_ARGS__);       \
		}                                                              \
	} while (false)

/*!
 * Error level logging.
 *
 * @ingroup comp_main
 */
#define COMP_ERROR(c, ...)                                                     \
	do {                                                                   \
		comp_compositor_print(c, __func__, __VA_ARGS__);               \
	} while (false)


#ifdef __cplusplus
}
#endif
