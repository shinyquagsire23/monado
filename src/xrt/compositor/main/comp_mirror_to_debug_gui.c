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

static bool
ensure_scratch(struct comp_mirror_to_debug_gui *m, struct vk_bundle *vk)
{
	VkResult ret;

	if (m->bounce.image != VK_NULL_HANDLE) {
		return true;
	}

	VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VkExtent2D extent = m->image_extent;

	ret = vk_create_image_simple( //
	    vk,                       // vk_bundle
	    extent,                   // extent
	    format,                   // format
	    usage,                    // usage
	    &m->bounce.mem,           // out_mem
	    &m->bounce.image);        // out_image
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vk_create_image_simple: %s", vk_result_string(ret));
		return false;
	}

	return true;
}


/*
 *
 * 'Exported' functions.
 *
 */

VkResult
comp_mirror_init(struct comp_mirror_to_debug_gui *m, struct vk_bundle *vk, VkExtent2D extent)
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
	    VK_FORMAT_R8G8B8A8_SRGB);        // vk_format

	ret = vk_cmd_pool_init(vk, &m->cmd_pool, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vk_cmd_pool_init: %s", vk_result_string(ret));
		return ret;
	}

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
                    VkExtent2D from_extent)
{
	VkResult ret;

	struct vk_image_readback_to_xf *wrap = NULL;

	if (!vk_image_readback_to_xf_pool_get_unused_frame(vk, m->pool, &wrap)) {
		return;
	}

	if (!ensure_scratch(m, vk)) {
		return;
	}

	struct vk_cmd_pool *pool = &m->cmd_pool;

	// For writing and submitting commands.
	vk_cmd_pool_lock(pool);

	VkCommandBuffer cmd;
	ret = vk_cmd_pool_create_and_begin_cmd_buffer_locked(vk, pool, 0, &cmd);
	if (ret != VK_SUCCESS) {
		vk_cmd_pool_unlock(pool);
		return;
	}

	// First mip view into the copy from image.
	struct vk_cmd_first_mip_image copy_from_fm_image = {
	    .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .base_array_layer = 0,
	    .image = from_image,
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

	// Copy arguments.
	struct vk_cmd_copy_image_info copy_info = {
	    .src.old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	    .src.src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
	    .src.src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
	    .src.fm_image = bounce_fm_image,

	    .dst.old_layout = wrap->layout,
	    .dst.src_access_mask = VK_ACCESS_HOST_READ_BIT,
	    .dst.src_stage_mask = VK_PIPELINE_STAGE_HOST_BIT,
	    .dst.fm_image = target_fm_image,

	    .size.w = m->image_extent.width,
	    .size.h = m->image_extent.height,
	};

	vk_cmd_copy_image_locked(vk, cmd, &copy_info);

	// Barrier arguments.
	VkImageSubresourceRange first_color_level_subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = 1,
	};

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
}

void
comp_mirror_fini(struct comp_mirror_to_debug_gui *m, struct vk_bundle *vk)
{
	// Remove u_var root as early as possible.
	u_var_remove_root(m);

	// Left eye readback
	vk_image_readback_to_xf_pool_destroy(vk, &m->pool);

	if (m->bounce.image != VK_NULL_HANDLE) {
		vk->vkFreeMemory(vk->device, m->bounce.mem, NULL);
		vk->vkDestroyImage(vk->device, m->bounce.image, NULL);
	}

	// Command pool for readback code.
	vk_cmd_pool_destroy(vk, &m->cmd_pool);

	// The frame timing widigt.
	u_frame_times_widget_teardown(&m->push_frame_times);

	// Destroy as late as possible.
	u_sink_debug_destroy(&m->debug_sink);
}
