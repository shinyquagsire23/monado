// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor mirroring code.
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */
#pragma once

#include "util/u_sink.h"
#include "vk/vk_image_readback_to_xf_pool.h"
#include "main/comp_compositor.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Helper struct for mirroring the compositors rendering to the debug ui,
 * which also enables recording. Currently embedded in @ref comp_renderer.
 *
 * @ingroup comp_main
 */
struct comp_mirror_to_debug_gui
{
	/*
	 * Hint: enable/disable is in c->mirroring_to_debug_gui. It is there
	 * because the ccomp_renderer struct is just a forward decl in the
	 * header and then defined properly in the comp_renderer.c file.
	 */

	struct u_frame_times_widget push_frame_times;

	float target_frame_time_ms;
	uint64_t last_push_ts_ns;
	int push_every_frame_out_of_X;

	struct u_sink_debug debug_sink;
	VkExtent2D image_extent;
	uint64_t sequence;

	struct vk_image_readback_to_xf_pool *pool;

	struct
	{
		VkImage image;
		VkImageView unorm_view;
		VkDeviceMemory mem;
	} bounce;

	struct
	{
		//! Private here for now.
		VkPipelineCache pipeline_cache;

		//! Descriptor pool for blit.
		VkDescriptorPool descriptor_pool;

		//! Descriptor set layout for compute.
		VkDescriptorSetLayout descriptor_set_layout;

		//! Pipeline layout used for compute distortion.
		VkPipelineLayout pipeline_layout;

		//! Doesn't depend on target so is static.
		VkPipeline pipeline;
	} blit;

	struct vk_cmd_pool cmd_pool;
};

/*!
 * Initialise the struct.
 *
 * @public @memberof comp_mirror_to_debug_gui
 */
VkResult
comp_mirror_init(struct comp_mirror_to_debug_gui *m,
                 struct vk_bundle *vk,
                 struct render_shaders *shaders,
                 VkExtent2D extent);

/*!
 * One time adding of the debug variables.
 *
 * @public @memberof comp_mirror_to_debug_gui
 */
void
comp_mirror_add_debug_vars(struct comp_mirror_to_debug_gui *m, struct comp_compositor *c);

/*!
 * Fixup various timing state.
 *
 * @public @memberof comp_mirror_to_debug_gui
 */
void
comp_mirror_fixup_ui_state(struct comp_mirror_to_debug_gui *m, struct comp_compositor *c);

/*!
 * Is this struct ready and capable of mirroring the image, can only
 * call @ref comp_mirror_do_blit if this function has returned true.
 *
 * @public @memberof comp_mirror_to_debug_gui
 */
bool
comp_mirror_is_ready_and_active(struct comp_mirror_to_debug_gui *m,
                                struct comp_compositor *c,
                                uint64_t predicted_display_time_ns);

/*!
 * Do the blit.
 *
 * @public @memberof comp_mirror_to_debug_gui
 */
void
comp_mirror_do_blit(struct comp_mirror_to_debug_gui *m,
                    struct vk_bundle *vk,
                    uint64_t predicted_display_time_ns,
                    VkImage from_image,
                    VkImageView from_view,
                    VkSampler from_sampler,
                    VkExtent2D from_extent,
                    struct xrt_normalized_rect from_rect);

/*!
 * Finalise the struct, frees and resources.
 *
 * @public @memberof comp_mirror_to_debug_gui
 */
void
comp_mirror_fini(struct comp_mirror_to_debug_gui *m, struct vk_bundle *vk);


#ifdef __cplusplus
}
#endif
