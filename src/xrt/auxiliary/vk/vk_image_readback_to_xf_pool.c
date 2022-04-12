// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Pool to read back VkImages from the gpu
 *
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_vk
 */

#include "os/os_threading.h"
#include "util/u_trace_marker.h"
#include "vk/vk_image_readback_to_xf_pool.h"


struct vk_image_readback_to_xf_pool
{
	struct os_mutex mutex;
	// We lazily allocate these as they're needed
	int num_images;
	struct vk_image_readback_to_xf images[READBACK_POOL_NUM_FRAMES];
	VkExtent2D extent;
	enum xrt_format desired_format;
};

static void
vk_xf_readback_release(struct xrt_frame *xf)
{
	XRT_TRACE_MARKER();
	struct vk_image_readback_to_xf *w = container_of(xf, struct vk_image_readback_to_xf, base_frame);

	os_mutex_lock(&w->pool->mutex);
	w->in_use = false;
	os_mutex_unlock(&w->pool->mutex);
}

// Creates a new frame, if there's room for one.
static void
vk_xf_readback_pool_try_create_new_frame(struct vk_bundle *vk, struct vk_image_readback_to_xf_pool *pool)
{
	// We ran out of frames.
	if (pool->num_images == READBACK_POOL_NUM_FRAMES) {
		return;
	}
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;

	VkExtent3D extent = {0};
	extent.width = pool->extent.width;
	extent.height = pool->extent.height;
	extent.depth = 1;

	VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	const VkMemoryPropertyFlags memory_property_flags = //
	    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |          //
	    VK_MEMORY_PROPERTY_HOST_CACHED_BIT |            //
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;            //

	VkResult res = vk_create_image_advanced( //
	    vk,                                  //
	    extent,                              //
	    VK_FORMAT_R8G8B8A8_SRGB,             //
	    VK_IMAGE_TILING_LINEAR,              //
	    usage,                               //
	    memory_property_flags,               //
	    &memory,                             //
	    &image);                             //

	(void)res;

	// Get layout of the image (including row pitch)
	const VkImageSubresource first_color_level_subresource = {
	    VK_IMAGE_ASPECT_COLOR_BIT, // aspectMask
	    0,                         // mipLevel
	    0,                         // arrayLayer
	};

	VkSubresourceLayout subResourceLayout = {0};

	vk->vkGetImageSubresourceLayout(    //
	    vk->device,                     // device
	    image,                          // image
	    &first_color_level_subresource, // pSubresource
	    &subResourceLayout);            // pLayout

	VkDeviceSize offset = subResourceLayout.offset;
	VkDeviceSize stride = subResourceLayout.rowPitch;

	// Map image memory and keep it mapped.
	uint8_t *data = NULL;
	res = vk->vkMapMemory( //
	    vk->device,        // device
	    memory,            // memory
	    0,                 // offset
	    VK_WHOLE_SIZE,     // size
	    0,                 // flags
	    (void **)&data);   // ppData



	int i = pool->num_images++;

	struct vk_image_readback_to_xf *im = &pool->images[i];
	im->pool = pool;
	im->image = image;
	im->memory = memory;
	im->image_extent = pool->extent;
	im->created = true;
	im->layout = VK_IMAGE_LAYOUT_UNDEFINED;
	im->base_frame.destroy = vk_xf_readback_release;
	im->base_frame.data = data + offset;
	im->base_frame.stride = stride;
	im->base_frame.width = extent.width;
	im->base_frame.height = extent.height;
	im->base_frame.size = stride * extent.height;
	im->base_frame.format = pool->desired_format;
}

/*
 * Called with pool lock held.
 */
static bool
find_created_not_used_wrap_locked(struct vk_image_readback_to_xf_pool *pool, struct vk_image_readback_to_xf **out)
{
	for (int i = 0; i < pool->num_images; i++) {
		struct vk_image_readback_to_xf *im = &pool->images[i];
		if (!im->created || im->in_use) {
			continue;
		}

		assert(im->base_frame.reference.count == 0);
		im->base_frame.reference.count = 1;
		im->in_use = true;
		*out = im;

		return true;
	}

	return false;
}

bool
vk_image_readback_to_xf_pool_get_unused_frame(struct vk_bundle *vk,
                                              struct vk_image_readback_to_xf_pool *pool,
                                              struct vk_image_readback_to_xf **out)
{
	XRT_TRACE_MARKER();

	assert(out != NULL && *out == NULL);

	// Look for now.
	os_mutex_lock(&pool->mutex);

	if (find_created_not_used_wrap_locked(pool, out)) {
		os_mutex_unlock(&pool->mutex);
		return true;
	}

	vk_xf_readback_pool_try_create_new_frame(vk, pool);

	bool found = find_created_not_used_wrap_locked(pool, out);

	// Finally unluck.
	os_mutex_unlock(&pool->mutex);

	if (!found) {
		U_LOG_W("Out of readback frames!");
	}

	return found;
}

void
vk_image_readback_to_xf_pool_create(struct vk_bundle *vk,
                                    VkExtent2D extent,
                                    struct vk_image_readback_to_xf_pool **out_pool,
                                    enum xrt_format desired_format)
{
	struct vk_image_readback_to_xf_pool *pool = U_TYPED_CALLOC(struct vk_image_readback_to_xf_pool);
	assert(desired_format == XRT_FORMAT_R8G8B8X8 || desired_format == XRT_FORMAT_R8G8B8A8);
	int ret = os_mutex_init(&pool->mutex);
	if (ret != 0) {
		assert(false);
	}
	pool->extent = extent;
	pool->num_images = 0;
	pool->desired_format = desired_format;

	*out_pool = pool;
}


void
vk_image_readback_to_xf_pool_destroy(struct vk_bundle *vk, struct vk_image_readback_to_xf_pool **pool_ptr)
{
	if (*pool_ptr == NULL) {
		return;
	}

	struct vk_image_readback_to_xf_pool *pool = *pool_ptr;
	*pool_ptr = NULL;

	for (int i = 0; i < pool->num_images; i++) {
		struct vk_image_readback_to_xf *im = &pool->images[i];
		if (!im->created) {
			continue;
		}

		vk->vkUnmapMemory( //
		    vk->device,    //
		    im->memory     //
		);
		vk->vkFreeMemory(vk->device, im->memory, NULL);
		vk->vkDestroyImage(vk->device, im->image, NULL);
	}

	os_mutex_destroy(&pool->mutex);

	free(pool);
}
