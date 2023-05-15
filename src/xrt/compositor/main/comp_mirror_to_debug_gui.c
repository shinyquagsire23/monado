// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor mirroring code.
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include "math/m_mathinclude.h"
#include "main/comp_mirror_to_debug_gui.h"


/*
 *
 * Helper functions.
 *
 */

/*!
 * If `COND` is not VK_SUCCESS returns false.
 */
#define C(c)                                                                                                           \
	do {                                                                                                           \
		VkResult ret = c;                                                                                      \
		if (ret != VK_SUCCESS) {                                                                               \
			comp_mirror_fini(m, vk);                                                                       \
			return ret;                                                                                    \
		}                                                                                                      \
	} while (false)

/*!
 * Calls `vkDestroy##TYPE` on `THING` if it is not `VK_NULL_HANDLE`, sets it to
 * `VK_NULL_HANDLE` afterwards.
 */
#define D(TYPE, THING)                                                                                                 \
	if (THING != VK_NULL_HANDLE) {                                                                                 \
		vk->vkDestroy##TYPE(vk->device, THING, NULL);                                                          \
		THING = VK_NULL_HANDLE;                                                                                \
	}

/*!
 * Calls `vkFree##TYPE` on `THING` if it is not `VK_NULL_HANDLE`, sets it to
 * `VK_NULL_HANDLE` afterwards.
 */
#define DF(TYPE, THING)                                                                                                \
	if (THING != VK_NULL_HANDLE) {                                                                                 \
		vk->vkFree##TYPE(vk->device, THING, NULL);                                                             \
		THING = VK_NULL_HANDLE;                                                                                \
	}


static VkResult
create_blit_descriptor_set_layout(struct vk_bundle *vk, VkDescriptorSetLayout *out_descriptor_set_layout)
{
	VkResult ret;

	VkDescriptorSetLayoutBinding set_layout_bindings[2] = {
	    {
	        .binding = 0,
	        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .descriptorCount = 1,
	        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	    },
	    {
	        .binding = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	        .descriptorCount = 1,
	        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	    },
	};

	VkDescriptorSetLayoutCreateInfo set_layout_info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	    .bindingCount = ARRAY_SIZE(set_layout_bindings),
	    .pBindings = set_layout_bindings,
	};

	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	ret = vk->vkCreateDescriptorSetLayout( //
	    vk->device,                        //
	    &set_layout_info,                  //
	    NULL,                              //
	    &descriptor_set_layout);           //
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateDescriptorSetLayout failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_descriptor_set_layout = descriptor_set_layout;

	return VK_SUCCESS;
}

static VkResult
create_blit_pipeline_layout(struct vk_bundle *vk,
                            VkDescriptorSetLayout descriptor_set_layout,
                            VkPipelineLayout *out_pipeline_layout)
{
	VkResult ret;

	VkPipelineLayoutCreateInfo pipeline_layout_info = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	    .flags = 0,
	    .setLayoutCount = 1,
	    .pSetLayouts = &descriptor_set_layout,
	    .pushConstantRangeCount = 1,
	    .pPushConstantRanges =
	        &(VkPushConstantRange){
	            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	            .offset = 0,
	            .size = sizeof(struct render_compute_blit_push_data),
	        },
	};

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	ret = vk->vkCreatePipelineLayout( //
	    vk->device,                   // device
	    &pipeline_layout_info,        // pCreateInfo
	    NULL,                         // pAllocator
	    &pipeline_layout);            // pPipelineLayout
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreatePipelineLayout failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_pipeline_layout = pipeline_layout;

	return VK_SUCCESS;
}

static bool
ensure_scratch(struct comp_mirror_to_debug_gui *m, struct vk_bundle *vk)
{
	VkResult ret;

	if (m->bounce.image != VK_NULL_HANDLE) {
		return true;
	}

	const VkFormat unorm_format = VK_FORMAT_R8G8B8A8_UNORM;
	const VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
	const VkExtent2D extent = m->image_extent;

	// Both usages are common.
	const VkImageUsageFlags unorm_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	// Very few cards support SRGB storage.
	const VkImageUsageFlags srgb_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	// Combination of both.
	const VkImageUsageFlags image_usage = unorm_usage | srgb_usage;

	ret = vk_create_image_mutable_rgba( //
	    vk,                             // vk_bundle
	    extent,                         // extent
	    image_usage,                    // usage
	    &m->bounce.mem,                 // out_device_memory
	    &m->bounce.image);              // out_image
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vk_create_image_mutable_rgba: %s", vk_result_string(ret));
		return false;
	}

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = VK_REMAINING_MIP_LEVELS,
	    .baseArrayLayer = 0,
	    .layerCount = VK_REMAINING_ARRAY_LAYERS,
	};

	ret = vk_create_view_usage( //
	    vk,                     // vk_bundle
	    m->bounce.image,        // image
	    view_type,              // type
	    unorm_format,           // format
	    unorm_usage,            // image_usage
	    subresource_range,      // subresource_range
	    &m->bounce.unorm_view); // out_image_view
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vk_create_view_usage: %s", vk_result_string(ret));
		return false;
	}

	return true;
}

static void
update_blit_descriptor_set(struct vk_bundle *vk,
                           VkSampler src_sampler,
                           VkImageView src_view,
                           VkImageView target_view,
                           VkDescriptorSet descriptor_set)
{
	VkDescriptorImageInfo src_image_info = {
	    .sampler = src_sampler,
	    .imageView = src_view,
	    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkDescriptorImageInfo target_image_info = {
	    .imageView = target_view,
	    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkWriteDescriptorSet write_descriptor_sets[2] = {
	    {
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = descriptor_set,
	        .dstBinding = 0,
	        .descriptorCount = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .pImageInfo = &src_image_info,
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = descriptor_set,
	        .dstBinding = 1,
	        .descriptorCount = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	        .pImageInfo = &target_image_info,
	    },
	};

	vk->vkUpdateDescriptorSets(            //
	    vk->device,                        //
	    ARRAY_SIZE(write_descriptor_sets), // descriptorWriteCount
	    write_descriptor_sets,             // pDescriptorWrites
	    0,                                 // descriptorCopyCount
	    NULL);
}

/*
 * For dispatching compute to the blit target, calculate the number of groups.
 */
static void
calc_dispatch_dims(const VkExtent2D extent, uint32_t *out_w, uint32_t *out_h)
{
	// Power of two divide and round up.
#define P2_DIVIDE_ROUND_UP(v, div) ((v + (div - 1)) / div)
	uint32_t w = P2_DIVIDE_ROUND_UP(extent.width, 8);
	uint32_t h = P2_DIVIDE_ROUND_UP(extent.height, 8);
#undef P2_DIVIDE_ROUND_UP

	*out_w = w;
	*out_h = h;
}


/*
 *
 * 'Exported' functions.
 *
 */

VkResult
comp_mirror_init(struct comp_mirror_to_debug_gui *m,
                 struct vk_bundle *vk,
                 struct render_shaders *shaders,
                 VkExtent2D extent)
{
	VkResult ret;

	// Do this init as early as possible.
	u_sink_debug_init(&m->debug_sink);

	double orig_width = extent.width;
	double orig_height = extent.height;

	double target_height = 1080;

	double mul = target_height / orig_height;

	// Casts seem to always round down; we don't want that here.
	m->image_extent.width = (uint32_t)(round(orig_width * mul));
	m->image_extent.height = (uint32_t)target_height;


	// We want the images to have even widths/heights so that libx264 can encode them properly; no other reason.
	if (m->image_extent.width % 2 == 1) {
		m->image_extent.width += 1;
	}

	vk_image_readback_to_xf_pool_create( //
	    vk,                              // vk_bundle
	    m->image_extent,                 // extent
	    &m->pool,                        // out_pool
	    XRT_FORMAT_R8G8B8X8,             // xrt_format
	    VK_FORMAT_R8G8B8A8_UNORM);       // vk_format

	ret = vk_cmd_pool_init(vk, &m->cmd_pool, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vk_cmd_pool_init: %s", vk_result_string(ret));
		comp_mirror_fini(m, vk);
		return ret;
	}

	struct vk_descriptor_pool_info blit_pool_info = {
	    .uniform_per_descriptor_count = 0,
	    .sampler_per_descriptor_count = 1,
	    .storage_image_per_descriptor_count = 1,
	    .storage_buffer_per_descriptor_count = 0,
	    .descriptor_count = 1,
	    .freeable = false,
	};

	C(vk_create_descriptor_pool(    //
	    vk,                         // vk_bundle
	    &blit_pool_info,            // info
	    &m->blit.descriptor_pool)); // out_descriptor_pool


	C(vk_create_pipeline_cache(vk, &m->blit.pipeline_cache));

	C(create_blit_descriptor_set_layout(vk, &m->blit.descriptor_set_layout));

	C(create_blit_pipeline_layout(     //
	    vk,                            // vk_bundle
	    m->blit.descriptor_set_layout, // descriptor_set_layout
	    &m->blit.pipeline_layout));    // out_pipeline_layout

	C(vk_create_compute_pipeline( //
	    vk,                       // vk_bundle
	    m->blit.pipeline_cache,   // pipeline_cache
	    shaders->blit_comp,       // shader
	    m->blit.pipeline_layout,  // pipeline_layout
	    NULL,                     // specialization_info
	    &m->blit.pipeline));      // out_compute_pipeline

	return VK_SUCCESS;
}

void
comp_mirror_add_debug_vars(struct comp_mirror_to_debug_gui *m, struct comp_compositor *c)
{
	// Reset state.
	m->push_every_frame_out_of_X = 2;

	// Init widigts.
	u_frame_times_widget_init(&m->push_frame_times, 0.f, 0.f);
	comp_mirror_fixup_ui_state(m, c);

	// Do the adding now.
	u_var_add_root(m, "Readback", true);

	u_var_add_bool(m, &c->mirroring_to_debug_gui, "Readback left eye to debug GUI");
	u_var_add_i32(m, &m->push_every_frame_out_of_X, "Push 1 frame out of every X frames");

	u_var_add_ro_f32(m, &m->push_frame_times.fps, "FPS (Readback)");
	u_var_add_f32_timing(m, m->push_frame_times.debug_var, "Frame Times (Readback)");

	u_var_add_sink_debug(m, &m->debug_sink, "Left view!");
}

void
comp_mirror_fixup_ui_state(struct comp_mirror_to_debug_gui *m, struct comp_compositor *c)
{
	// One out of every zero frames is not what we want!
	// Also one out of every negative two frames, etc. is nonsensical
	if (m->push_every_frame_out_of_X < 1) {
		m->push_every_frame_out_of_X = 1;
	}

	float nominal_frame_interval_ms = (float)time_ns_to_ms_f(c->settings.nominal_frame_interval_ns);

	m->target_frame_time_ms = (float)m->push_every_frame_out_of_X * nominal_frame_interval_ms;

	m->push_frame_times.debug_var->reference_timing = m->target_frame_time_ms;
	m->push_frame_times.debug_var->range = m->target_frame_time_ms;
}

bool
comp_mirror_is_ready_and_active(struct comp_mirror_to_debug_gui *m,
                                struct comp_compositor *c,
                                uint64_t predicted_display_time_ns)
{
	if (!c->mirroring_to_debug_gui || !u_sink_debug_is_active(&m->debug_sink)) {
		return false;
	}

	double diff_ms = time_ns_to_ms_f(predicted_display_time_ns - m->last_push_ts_ns);

	// Completely unscientific - lower values probably works fine too.
	// I figure we don't have very many 500Hz displays and this woorks great for 120-144hz
	double slop_ms = 2;

	if (diff_ms < m->target_frame_time_ms - slop_ms) {
		return false;
	}

	// Set the last time to the frame that is being displayed.
	m->last_push_ts_ns = predicted_display_time_ns;

	return true;
}

void
comp_mirror_do_blit(struct comp_mirror_to_debug_gui *m,
                    struct vk_bundle *vk,
                    uint64_t predicted_display_time_ns,
                    VkImage from_image,
                    VkImageView from_view,
                    VkSampler from_sampler,
                    VkExtent2D from_extent,
                    struct xrt_normalized_rect from_rect)
{
	VkResult ret;

	struct vk_image_readback_to_xf *wrap = NULL;

	if (!vk_image_readback_to_xf_pool_get_unused_frame(vk, m->pool, &wrap)) {
		return;
	}

	if (!ensure_scratch(m, vk)) {
		return;
	}

	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	ret = vk_create_descriptor_set(    //
	    vk,                            //
	    m->blit.descriptor_pool,       // descriptor_pool
	    m->blit.descriptor_set_layout, // descriptor_set_layout
	    &descriptor_set);              // descriptor_set
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vk_create_descriptor_set: %s", vk_result_string(ret));
		return;
	}

	struct vk_cmd_pool *pool = &m->cmd_pool;

	// For writing and submitting commands.
	vk_cmd_pool_lock(pool);

	VkCommandBuffer cmd;
	ret = vk_cmd_pool_create_and_begin_cmd_buffer_locked(vk, pool, 0, &cmd);
	if (ret != VK_SUCCESS) {
		vk->vkResetDescriptorPool(vk->device, m->blit.descriptor_pool, 0);
		vk_cmd_pool_unlock(pool);
		return;
	}

	// Barrier arguments.
	VkImageSubresourceRange first_color_level_subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = 1,
	};

	// First mip view into the bounce image.
	struct vk_cmd_first_mip_image bounce_fm_image = {
	    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .base_array_layer = 0,
	    .image = m->bounce.image,
	};

	// First mip view into the target image.
	struct vk_cmd_first_mip_image target_fm_image = {
	    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .base_array_layer = 0,
	    .image = wrap->image,
	};

	VkImageLayout old_layout;
	VkPipelineStageFlags src_stage_mask;
	VkAccessFlags src_access_mask;

#if 0
	// First mip view into the copy from image.
	struct vk_cmd_first_mip_image copy_from_fm_image = {
	    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .base_array_layer = 0,
	    .image = from_image,
	};

	// Not completely removed just in case we need to go back on some platforms.
	if (!use_compute_blit) {
		// Blit arguments.
		struct vk_cmd_blit_image_info blit_info = {
		    .src.old_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		    .src.src_access_mask = VK_ACCESS_SHADER_READ_BIT,
		    .src.src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		    .src.fm_image = copy_from_fm_image,

		    .src.rect.offset = {0, 0},
		    .src.rect.extent = {from_extent.width, from_extent.height},

		    .dst.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
		    .dst.src_access_mask = VK_ACCESS_TRANSFER_READ_BIT,
		    .dst.src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
		    .dst.fm_image = bounce_fm_image,

		    .dst.rect.offset = {0, 0},
		    .dst.rect.extent = {m->image_extent.width, m->image_extent.height},
		};

		vk_cmd_blit_image_locked(vk, cmd, &blit_info);

		old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
		src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else
#endif
	{
		// Barrier bounce image so it can be safely written to.
		vk_cmd_image_barrier_locked(              //
		    vk,                                   // vk_bundle
		    cmd,                                  // cmdbuffer
		    bounce_fm_image.image,                // image
		    0,                                    // srcAccessMask
		    VK_ACCESS_SHADER_WRITE_BIT,           // dstAccessMask
		    VK_IMAGE_LAYOUT_UNDEFINED,            // oldImageLayout
		    VK_IMAGE_LAYOUT_GENERAL,              // newImageLayout
		    VK_PIPELINE_STAGE_TRANSFER_BIT,       // srcStageMask
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // dstStageMask
		    first_color_level_subresource_range); // subresourceRange

		update_blit_descriptor_set( //
		    vk,                     //
		    from_sampler,           //
		    from_view,              //
		    m->bounce.unorm_view,   //
		    descriptor_set);        //

		vk->vkCmdBindPipeline(              //
		    cmd,                            // commandBuffer
		    VK_PIPELINE_BIND_POINT_COMPUTE, // pipelineBindPoint
		    m->blit.pipeline);              // pipeline

		vk->vkCmdBindDescriptorSets(        //
		    cmd,                            // commandBuffer
		    VK_PIPELINE_BIND_POINT_COMPUTE, // pipelineBindPoint
		    m->blit.pipeline_layout,        // layout
		    0,                              // firstSet
		    1,                              // descriptorSetCount
		    &descriptor_set,                // pDescriptorSets
		    0,                              // dynamicOffsetCount
		    NULL);                          // pDynamicOffsets

		struct render_compute_blit_push_data constants = {
		    from_rect,
		    {{0, 0}, {m->image_extent.width, m->image_extent.height}},
		};

		vk->vkCmdPushConstants(          //
		    cmd,                         //
		    m->blit.pipeline_layout,     //
		    VK_SHADER_STAGE_COMPUTE_BIT, //
		    0,                           //
		    sizeof(constants),           //
		    &constants);                 //

		uint32_t w = 0, h = 0;
		calc_dispatch_dims(m->image_extent, &w, &h);
		assert(w != 0 && h != 0);

		vk->vkCmdDispatch( //
		    cmd,           // commandBuffer
		    w,             // groupCountX
		    h,             // groupCountY
		    1);            // groupCountZ

		old_layout = VK_IMAGE_LAYOUT_GENERAL;
		src_access_mask = VK_ACCESS_SHADER_WRITE_BIT;
		src_stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}

	// Copy arguments.
	struct vk_cmd_copy_image_info copy_info = {
	    .src.old_layout = old_layout,
	    .src.src_access_mask = src_access_mask,
	    .src.src_stage_mask = src_stage_mask,
	    .src.fm_image = bounce_fm_image,

	    .dst.old_layout = wrap->layout,
	    .dst.src_access_mask = VK_ACCESS_HOST_READ_BIT,
	    .dst.src_stage_mask = VK_PIPELINE_STAGE_HOST_BIT,
	    .dst.fm_image = target_fm_image,

	    .size.w = m->image_extent.width,
	    .size.h = m->image_extent.height,
	};

	vk_cmd_copy_image_locked(vk, cmd, &copy_info);

	// Barrier readback image to host so we can safely read
	vk_cmd_image_barrier_locked(              //
	    vk,                                   // vk_bundle
	    cmd,                                  // cmdbuffer
	    wrap->image,                          // image
	    VK_ACCESS_TRANSFER_WRITE_BIT,         // srcAccessMask
	    VK_ACCESS_HOST_READ_BIT,              // dstAccessMask
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // oldImageLayout
	    VK_IMAGE_LAYOUT_GENERAL,              // newImageLayout
	    VK_PIPELINE_STAGE_TRANSFER_BIT,       // srcStageMask
	    VK_PIPELINE_STAGE_HOST_BIT,           // dstStageMask
	    first_color_level_subresource_range); // subresourceRange

	// Done writing commands, submit to queue, waits for command to finish.
	ret = vk_cmd_pool_end_submit_wait_and_free_cmd_buffer_locked(vk, pool, cmd);

	// Done submitting commands.
	vk_cmd_pool_unlock(pool);

	// Check results from submit.
	if (ret != VK_SUCCESS) {
		//! @todo Better handling of error?
		VK_ERROR(vk, "vk_cmd_pool_end_submit_wait_and_free_cmd_buffer_locked: %s", vk_result_string(ret));
	}

	wrap->base_frame.source_timestamp = wrap->base_frame.timestamp = predicted_display_time_ns;
	wrap->base_frame.source_id = m->sequence++;

	struct xrt_frame *frame = &wrap->base_frame;
	wrap = NULL;

	u_sink_debug_push_frame(&m->debug_sink, frame);

	u_frame_times_widget_push_sample(&m->push_frame_times, predicted_display_time_ns);

	xrt_frame_reference(&frame, NULL);

	// Tidies the descriptor we created.
	vk->vkResetDescriptorPool(vk->device, m->blit.descriptor_pool, 0);
}

void
comp_mirror_fini(struct comp_mirror_to_debug_gui *m, struct vk_bundle *vk)
{
	// Remove u_var root as early as possible.
	u_var_remove_root(m);

	// Left eye readback
	vk_image_readback_to_xf_pool_destroy(vk, &m->pool);

	// Bounce image resources.
	D(ImageView, m->bounce.unorm_view);
	D(Image, m->bounce.image);
	DF(Memory, m->bounce.mem);

	// Command pool for readback code.
	vk_cmd_pool_destroy(vk, &m->cmd_pool);

	// Destroy blit shader Vulkan resources.
	D(Pipeline, m->blit.pipeline);
	D(PipelineLayout, m->blit.pipeline_layout);
	D(PipelineCache, m->blit.pipeline_cache);
	D(DescriptorPool, m->blit.descriptor_pool);
	D(DescriptorSetLayout, m->blit.descriptor_set_layout);

	// The frame timing widigt.
	u_frame_times_widget_teardown(&m->push_frame_times);

	// Destroy as late as possible.
	u_sink_debug_destroy(&m->debug_sink);
}
