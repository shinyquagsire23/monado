// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main compositor written using Vulkan header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "xrt/xrt_gfx_vk.h"
#include "xrt/xrt_config_build.h"

#include "util/u_threading.h"
#include "util/u_index_fifo.h"
#include "util/u_logging.h"

#include "vk/vk_image_allocator.h"

#include "main/comp_settings.h"
#include "main/comp_window.h"
#include "main/comp_renderer.h"
#include "main/comp_target.h"

#include "render/comp_render.h"


#ifdef __cplusplus
extern "C" {
#endif

#define NUM_FRAME_TIMES 50
#define COMP_MAX_LAYERS 16

/*
 *
 * Structs
 *
 */

/*!
 * A single swapchain image, holds the needed state for tracking image usage.
 *
 * @ingroup comp_main
 * @see comp_swapchain
 */
struct comp_swapchain_image
{
	//! Sampler used by the renderer and distortion code.
	VkSampler sampler;
	VkSampler repeat_sampler;
	//! Views used by the renderer and distortion code, for each array
	//! layer.
	struct
	{
		VkImageView *alpha;
		VkImageView *no_alpha;
	} views;
	//! The number of array slices in a texture, 1 == regular 2D texture.
	size_t array_size;
};

/*!
 * A swapchain that is almost a one to one mapping to a OpenXR swapchain.
 *
 * Not used by the window backend that uses the vk_swapchain to render to.
 *
 * @ingroup comp_main
 * @implements xrt_swapchain_native
 * @see comp_compositor
 */
struct comp_swapchain
{
	struct xrt_swapchain_native base;

	struct comp_compositor *c;

	struct vk_image_collection vkic;
	struct comp_swapchain_image images[XRT_MAX_SWAPCHAIN_IMAGES];

	/*!
	 * This fifo is used to always give out the oldest image to acquire
	 * image, this should probably be made even smarter.
	 */
	struct u_index_fifo fifo;
};

/*!
 * A single layer.
 *
 * @ingroup comp_main
 * @see comp_layer_slot
 */
struct comp_layer
{
	/*!
	 * Up to two compositor swapchains referenced per layer.
	 *
	 * Unused elements should be set to null.
	 */
	struct comp_swapchain *scs[2];

	/*!
	 * All basic (trivially-serializable) data associated with a layer.
	 */
	struct xrt_layer_data data;
};

/*!
 * A stack of layers.
 *
 * @ingroup comp_main
 * @see comp_compositor
 */
struct comp_layer_slot
{
	enum xrt_blend_mode env_blend_mode;

	struct comp_layer layers[COMP_MAX_LAYERS];

	uint32_t num_layers;
};

/*!
 * State to emulate state transitions correctly.
 *
 * @ingroup comp_main
 */
enum comp_state
{
	COMP_STATE_UNINITIALIZED = 0,
	COMP_STATE_READY = 1,
	COMP_STATE_PREPARED = 2,
	COMP_STATE_VISIBLE = 3,
	COMP_STATE_FOCUSED = 4,
};

struct comp_shaders
{
	VkShaderModule mesh_vert;
	VkShaderModule mesh_frag;

	VkShaderModule equirect1_vert;
	VkShaderModule equirect1_frag;

	VkShaderModule equirect2_vert;
	VkShaderModule equirect2_frag;

	VkShaderModule layer_vert;
	VkShaderModule layer_frag;
};

/*!
 * Main compositor struct tying everything in the compositor together.
 *
 * @ingroup comp_main
 * @implements xrt_compositor_native
 */
struct comp_compositor
{
	struct xrt_compositor_native base;

	struct xrt_system_compositor system;

	//! Renderer helper.
	struct comp_renderer *r;

	//! The target we are displaying to.
	struct comp_target *target;

	//! The device we are displaying to.
	struct xrt_device *xdev;

	//! The settings.
	struct comp_settings settings;

	//! Vulkan bundle of things.
	struct vk_bundle vk;

	//! Vulkan shaders that the compositor uses.
	struct comp_shaders shaders;

	//! Timestamp of last-rendered (immersive) frame.
	int64_t last_frame_time_ns;

	//! State for generating the correct set of events.
	enum comp_state state;

	//! Triple buffered layer stacks.
	struct comp_layer_slot slots[3];

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

	struct
	{
		//! Current Index for times_ns.
		int index;

		//! Timestamps of last-rendered (immersive) frames.
		int64_t times_ns[NUM_FRAME_TIMES];

		//! Frametimes between last-rendered (immersive) frames.
		float timings_ms[NUM_FRAME_TIMES];

		//! Average FPS of last NUM_FRAME_TIMES rendered frames.
		float fps;

		struct u_var_timing *debug_var;
	} compositor_frame_times;

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

	struct
	{
		//! Thread object for safely destroying swapchain.
		struct u_threading_stack destroy_swapchains;
	} threading;


	struct comp_resources nr;

	//! To insure only one compositor is created.
	bool compositor_created;
};


/*
 *
 * Functions and helpers.
 *
 */

/*!
 * Check if the compositor can create swapchains with this format.
 */
bool
comp_is_format_supported(struct comp_compositor *c, VkFormat format);

/*!
 * Convenience function to convert a xrt_swapchain to a comp_swapchain.
 *
 * @private @memberof comp_swapchain
 */
static inline struct comp_swapchain *
comp_swapchain(struct xrt_swapchain *xsc)
{
	return (struct comp_swapchain *)xsc;
}

/*!
 * Convenience function to convert a xrt_compositor to a comp_compositor.
 *
 * @private @memberof comp_compositor
 */
static inline struct comp_compositor *
comp_compositor(struct xrt_compositor *xc)
{
	return (struct comp_compositor *)xc;
}

/*!
 * Do garbage collection, destroying any resources that has been scheduled for
 * destruction from other threads.
 *
 * @public @memberof comp_compositor
 */
void
comp_compositor_garbage_collect(struct comp_compositor *c);

/*!
 * A compositor function that is implemented in the swapchain code.
 *
 * @public @memberof comp_compositor
 */
xrt_result_t
comp_swapchain_create(struct xrt_compositor *xc,
                      const struct xrt_swapchain_create_info *info,
                      struct xrt_swapchain **out_xsc);

/*!
 * A compositor function that is implemented in the swapchain code.
 *
 * @public @memberof comp_compositor
 */
xrt_result_t
comp_swapchain_import(struct xrt_compositor *xc,
                      const struct xrt_swapchain_create_info *info,
                      struct xrt_image_native *native_images,
                      uint32_t num_images,
                      struct xrt_swapchain **out_xsc);

/*!
 * Swapchain destruct is delayed until it is safe to destroy them, this function
 * does the actual destruction and is called from @ref
 * comp_compositor_garbage_collect.
 *
 * @private @memberof comp_swapchain
 */
void
comp_swapchain_really_destroy(struct comp_swapchain *sc);

/*!
 * Loads all of the shaders that the compositor uses.
 */
bool
comp_shaders_load(struct vk_bundle *vk, struct comp_shaders *s);

/*!
 * Loads all of the shaders that the compositor uses.
 */
void
comp_shaders_close(struct vk_bundle *vk, struct comp_shaders *s);

/*!
 * Spew level logging.
 *
 * @relates comp_compositor
 */
#define COMP_SPEW(c, ...) U_LOG_IFL_T(c->settings.log_level, __VA_ARGS__);

/*!
 * Debug level logging.
 *
 * @relates comp_compositor
 */
#define COMP_DEBUG(c, ...) U_LOG_IFL_D(c->settings.log_level, __VA_ARGS__);

/*!
 * Info level logging.
 *
 * @relates comp_compositor
 */
#define COMP_INFO(c, ...) U_LOG_IFL_I(c->settings.log_level, __VA_ARGS__);

/*!
 * Warn level logging.
 *
 * @relates comp_compositor
 */
#define COMP_WARN(c, ...) U_LOG_IFL_W(c->settings.log_level, __VA_ARGS__);

/*!
 * Error level logging.
 *
 * @relates comp_compositor
 */
#define COMP_ERROR(c, ...) U_LOG_IFL_E(c->settings.log_level, __VA_ARGS__);

/*!
 * Mode printing.
 *
 * @relates comp_compositor
 */
#define COMP_PRINT_MODE(c, ...)                                                                                        \
	if (c->settings.print_modes) {                                                                                 \
		U_LOG_I(__VA_ARGS__);                                                                                  \
	}


#ifdef __cplusplus
}
#endif
