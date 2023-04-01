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


#ifdef __cplusplus
}
#endif
