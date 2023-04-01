// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Command pool helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup aux_vk
 */

#pragma once

#include "vk/vk_helpers.h"
#include "vk/vk_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Struct(s)
 *
 */

/*!
 * Small helper to manage lock around a command pool.
 *
 * @ingroup aux_vk
 */
struct vk_cmd_pool
{
	VkCommandPool pool;
	struct os_mutex mutex;
};


/*
 *
 * Functions.
 *
 */

/*!
 * Create a command buffer pool.
 *
 * @public @memberof vk_cmd_pool
 */
XRT_CHECK_RESULT VkResult
vk_cmd_pool_init(struct vk_bundle *vk, struct vk_cmd_pool *pool, VkCommandPoolCreateFlags flags);

/*!
 * Destroy a command buffer pool, lock must not be held, externally
 * synchronizable with all other pool commands.
 *
 * @public @memberof vk_cmd_pool
 */
void
vk_cmd_pool_destroy(struct vk_bundle *vk, struct vk_cmd_pool *pool);

/*!
 * Create a command buffer, call with the pool mutex held.
 *
 * @pre Command pool lock must be held, see @ref vk_cmd_pool_lock.
 *
 * @public @memberof vk_cmd_pool
 */
XRT_CHECK_RESULT VkResult
vk_cmd_pool_create_cmd_buffer_locked(struct vk_bundle *vk, struct vk_cmd_pool *pool, VkCommandBuffer *out_cmd_buffer);

/*!
 * Create a command buffer and also begin it, call with the pool mutex held.
 *
 * @pre Command pool lock must be held.
 *
 * @public @memberof vk_cmd_pool
 */
XRT_CHECK_RESULT VkResult
vk_cmd_pool_create_and_begin_cmd_buffer_locked(struct vk_bundle *vk,
                                               struct vk_cmd_pool *pool,
                                               VkCommandBufferUsageFlags flags,
                                               VkCommandBuffer *out_cmd_buffer);

/*!
 * Submit to the vulkan queue, will take the queue mutex.
 *
 * @pre Command pool lock must be held, see @ref vk_cmd_pool_lock.
 *
 * @public @memberof vk_cmd_pool
 */
XRT_CHECK_RESULT VkResult
vk_cmd_pool_submit_cmd_buffer_locked(struct vk_bundle *vk, struct vk_cmd_pool *pool, VkCommandBuffer cmd_buffer);

/*!
 * A do everything submit function, will take the queue mutex. Will create a
 * fence and wait on the commands to complete. Will also end and destroy the
 * passed in command buffer.
 *
 * @pre Command pool lock must be held, see @ref vk_cmd_pool_lock.
 *
 * Calls:
 * * vkEndCommandBuffer
 * * vkCreateFence
 * * vkWaitForFences
 * * vkDestroyFence
 * * vkFreeCommandBuffers
 *
 * @public @memberof vk_cmd_pool
 */
XRT_CHECK_RESULT static inline VkResult
vk_cmd_pool_end_submit_wait_and_free_cmd_buffer_locked(struct vk_bundle *vk,
                                                       struct vk_cmd_pool *pool,
                                                       VkCommandBuffer cmd_buffer)
{
	return vk_cmd_end_submit_wait_and_free_cmd_buffer_locked(vk, pool->pool, cmd_buffer);
}

/*!
 * Lock the command pool, needed for creating command buffers, filling out
 * commands on any command buffers created from this pool and submitting any
 * command buffers created from this pool to a VkQueue.
 *
 * @public @memberof vk_cmd_pool
 */
static inline void
vk_cmd_pool_lock(struct vk_cmd_pool *pool)
{
	os_mutex_lock(&pool->mutex);
}

/*!
 * Unlock the command pool.
 *
 * @public @memberof vk_cmd_pool
 */
static inline void
vk_cmd_pool_unlock(struct vk_cmd_pool *pool)
{
	os_mutex_unlock(&pool->mutex);
}

/*!
 * Locks, calls @ref vk_cmd_pool_create_cmd_buffer_locked, and then unlocks the
 * command pool.
 *
 * @public @memberof vk_cmd_pool
 */
XRT_CHECK_RESULT static inline VkResult
vk_cmd_pool_create_cmd_buffer(struct vk_bundle *vk, struct vk_cmd_pool *pool, VkCommandBuffer *out_cmd_buffer)
{
	vk_cmd_pool_lock(pool);
	VkResult ret = vk_cmd_pool_create_cmd_buffer_locked(vk, pool, out_cmd_buffer);
	vk_cmd_pool_unlock(pool);
	return ret;
}

/*!
 * Locks, calls @ref vk_cmd_pool_create_and_begin_cmd_buffer_locked, and then
 * unlocks the command pool.
 *
 * @public @memberof vk_cmd_pool
 */
XRT_CHECK_RESULT static inline VkResult
vk_cmd_pool_create_and_begin_cmd_buffer(struct vk_bundle *vk,
                                        struct vk_cmd_pool *pool,
                                        VkCommandBufferUsageFlags flags,
                                        VkCommandBuffer *out_cmd_buffer)
{
	vk_cmd_pool_lock(pool);
	VkResult ret = vk_cmd_pool_create_and_begin_cmd_buffer_locked(vk, pool, flags, out_cmd_buffer);
	vk_cmd_pool_unlock(pool);
	return ret;
}

/*!
 * Locks, calls @ref vk_cmd_pool_submit_locked, and then unlocks the command
 * pool. Will during the call take the queue lock and release it.
 *
 * @public @memberof vk_cmd_pool
 */
XRT_CHECK_RESULT static inline VkResult
vk_cmd_pool_submit(
    struct vk_bundle *vk, struct vk_cmd_pool *pool, uint32_t count, const VkSubmitInfo *infos, VkFence fence)
{
	vk_cmd_pool_lock(pool);
	VkResult ret = vk_cmd_submit_locked(vk, count, infos, fence);
	vk_cmd_pool_unlock(pool);
	return ret;
}

/*!
 * Locks, calls @ref vk_cmd_pool_submit_cmd_buffer_locked, and then unlocks the
 * command pool. Will during the call take the queue lock and release it.
 *
 * @public @memberof vk_cmd_pool
 */
XRT_CHECK_RESULT static inline VkResult
vk_cmd_pool_submit_cmd_buffer(struct vk_bundle *vk, struct vk_cmd_pool *pool, VkCommandBuffer cmd_buffer)
{
	vk_cmd_pool_lock(pool);
	VkResult ret = vk_cmd_pool_submit_cmd_buffer_locked(vk, pool, cmd_buffer);
	vk_cmd_pool_unlock(pool);
	return ret;
}

/*!
 * Locks, calls @ref vk_cmd_pool_end_submit_wait_and_free_cmd_buffer_locked, and
 * then unlocks the command pool. Will during the call take the queue lock and
 * release it.
 *
 * @public @memberof vk_cmd_pool
 */
XRT_CHECK_RESULT static inline VkResult
vk_cmd_pool_end_submit_wait_and_free_cmd_buffer(struct vk_bundle *vk,
                                                struct vk_cmd_pool *pool,
                                                VkCommandBuffer cmd_buffer)
{
	vk_cmd_pool_lock(pool);
	VkResult ret = vk_cmd_pool_end_submit_wait_and_free_cmd_buffer_locked(vk, pool, cmd_buffer);
	vk_cmd_pool_unlock(pool);
	return ret;
}


#ifdef __cplusplus
}
#endif
