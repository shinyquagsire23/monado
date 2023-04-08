// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Command buffer helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup aux_vk
 */

#pragma once

#include "vk/vk_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Struct(s).
 *
 */

/*!
 * A similar struct to VkImageSubresourceRange and VkImageSubresourceLayers
 * expect for this it's implied that it's only the first mip-level and only one
 * array layer used for the operation.
 *
 * @ingroup aux_vk
 */
struct vk_cmd_first_mip_image
{
	uint32_t base_array_layer;
	VkAccessFlags aspect_mask;
	VkImage image;
};

/*!
 * Argument struct for @ref vk_cmd_copy_image_locked.
 *
 * See @ref struct vk_cmd_first_mip_image for array and mip selection rules.
 *
 * @ingroup aux_vk
 */
struct vk_cmd_copy_image_info
{
	struct
	{
		VkImageLayout old_layout;
		VkAccessFlags src_access_mask;
		VkPipelineStageFlags src_stage_mask;
		struct vk_cmd_first_mip_image fm_image;
	} src;
	struct
	{
		VkImageLayout old_layout;
		VkAccessFlags src_access_mask;
		VkPipelineStageFlags src_stage_mask;
		struct vk_cmd_first_mip_image fm_image;
	} dst;
	struct xrt_size size;
};

/*!
 * Argument struct for @ref vk_cmd_blit_image_locked.
 *
 * See @ref struct vk_cmd_first_mip_image for array and mip selection rules.
 *
 * @ingroup aux_vk
 */
struct vk_cmd_blit_image_info
{
	struct
	{
		VkImageLayout old_layout;
		VkAccessFlags src_access_mask;
		VkPipelineStageFlags src_stage_mask;
		struct xrt_rect rect;
		struct vk_cmd_first_mip_image fm_image;
	} src;
	struct
	{
		VkImageLayout old_layout;
		VkAccessFlags src_access_mask;
		VkPipelineStageFlags src_stage_mask;
		struct xrt_rect rect;
		struct vk_cmd_first_mip_image fm_image;
	} dst;
};

/*!
 * Argument struct for @ref vk_cmd_blit_images_side_by_side_locked.
 *
 * See @ref struct vk_cmd_first_mip_image for array and mip selection rules.
 *
 * @ingroup aux_vk
 */
struct vk_cmd_blit_images_side_by_side_info
{
	struct
	{
		VkImageLayout old_layout;
		VkAccessFlags src_access_mask;
		VkPipelineStageFlags src_stage_mask;
		struct xrt_rect rect;
		struct vk_cmd_first_mip_image fm_image;
	} src[2];
	struct
	{
		VkImageLayout old_layout;
		VkAccessFlags src_access_mask;
		VkPipelineStageFlags src_stage_mask;
		struct xrt_size size;
		struct vk_cmd_first_mip_image fm_image;
	} dst;
};


/*
 *
 * Command buffer functions.
 *
 */

/*!
 * Create a command buffer, the pool must be locked or ensured that only this
 * thread is accessing it.
 *
 * @pre The look for the command pool must be held, or the code must
 * ensure that only the calling thread is accessing the command pool.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_cmd_create_cmd_buffer_locked(struct vk_bundle *vk, VkCommandPool pool, VkCommandBuffer *out_cmd_buffer);

/*!
 * Create and begin a command buffer, the pool must be locked or ensured that
 * only this thread is accessing it.
 *
 * @pre The look for the command pool must be held, or the code must
 * ensure that only the calling thread is accessing the command pool.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_cmd_create_and_begin_cmd_buffer_locked(struct vk_bundle *vk,
                                          VkCommandPool pool,
                                          VkCommandBufferUsageFlags flags,
                                          VkCommandBuffer *out_cmd_buffer);

/*!
 * Very small helper to submit a command buffer, the `_locked` suffix refers to
 * the command pool not the queue, the queue lock will be taken during the queue
 * submit call, then released. The pool must be locked or ensured that only this
 * thread is accessing it.
 *
 * @pre The look for the command pool must be held, or the code must
 * ensure that only the calling thread is accessing the command pool.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_cmd_submit_locked(struct vk_bundle *vk, uint32_t count, const VkSubmitInfo *infos, VkFence fence);

/*!
 * A do everything command buffer submission function, the `_locked` suffix
 * refers to the command pool not the queue, the queue lock will be taken during
 * the queue submit call, then released. The pool must be locked or ensured that
 * only this thread is accessing it.
 *
 * @pre The look for the command pool must be held, or the code must
 * ensure that only the calling thread is accessing the command pool.
 *
 * * Creates a new fence.
 * * Takes queue lock.
 * * Submits @p cmd_buffer to the queue, along with the fence.
 * * Release queue lock.
 * * Waits for the fence to complete.
 * * Destroys the fence.
 * * Destroy @p cmd_buffer.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_cmd_end_submit_wait_and_free_cmd_buffer_locked(struct vk_bundle *vk, VkCommandPool pool, VkCommandBuffer cmd_buffer);


/*
 *
 * Copy and blit helpers.
 *
 */

/*!
 * Performs a copy of a image into a destination image, also does needed barrier
 * operation needed to get images ready for transfer operations. Images will be
 * left in the layout and pipeline needed for transfers.
 *
 * * Src image(s): VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
 * * Dst image(s): VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
 *
 * @ingroup aux_vk
 */
void
vk_cmd_copy_image_locked(struct vk_bundle *vk, VkCommandBuffer cmd_buffer, const struct vk_cmd_copy_image_info *info);

/*!
 * Performs a blit of a image into a destination image, also does needed barrier
 * operation needed to get images ready for transfer operations. Images will be
 * left in the layout and pipeline needed for transfers.
 *
 * * Src image(s): VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
 * * Dst image(s): VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
 *
 * @ingroup aux_vk
 */
void
vk_cmd_blit_image_locked(struct vk_bundle *vk, VkCommandBuffer cmd_buffer, const struct vk_cmd_blit_image_info *info);

/*!
 * Performs a blit of two images to side by side on a destination image, also
 * does needed barrier operation needed to get images ready for transfer
 * operations. Images will be left in the layout and pipeline needed for
 * transfers.
 *
 * * Src image(s): VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
 * * Dst image(s): VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
 *
 * @ingroup aux_vk
 */
void
vk_cmd_blit_images_side_by_side_locked(struct vk_bundle *vk,
                                       VkCommandBuffer cmd_buffer,
                                       const struct vk_cmd_blit_images_side_by_side_info *info);


#ifdef __cplusplus
}
#endif
