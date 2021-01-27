// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Swapchain code for the main compositor.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include "util/u_misc.h"
#include "util/u_handles.h"

#include "main/comp_compositor.h"

#include <xrt/xrt_handles.h>
#include <xrt/xrt_config_os.h>

#include <stdio.h>
#include <stdlib.h>


/*
 *
 * Swapchain member functions.
 *
 */

static void
swapchain_destroy(struct xrt_swapchain *xsc)
{
	struct comp_swapchain *sc = comp_swapchain(xsc);

	COMP_SPEW(sc->c, "DESTROY");

	u_threading_stack_push(&sc->c->threading.destroy_swapchains, sc);
}

static xrt_result_t
swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *out_index)
{
	struct comp_swapchain *sc = comp_swapchain(xsc);

	COMP_SPEW(sc->c, "ACQUIRE_IMAGE");

	// Returns negative on empty fifo.
	int res = u_index_fifo_pop(&sc->fifo, out_index);
	if (res >= 0) {
		return XRT_SUCCESS;
	}
	return XRT_ERROR_NO_IMAGE_AVAILABLE;
}

static xrt_result_t
swapchain_wait_image(struct xrt_swapchain *xsc, uint64_t timeout, uint32_t index)
{
	struct comp_swapchain *sc = comp_swapchain(xsc);

	COMP_SPEW(sc->c, "WAIT_IMAGE");
	return XRT_SUCCESS;
}

static xrt_result_t
swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	struct comp_swapchain *sc = comp_swapchain(xsc);

	COMP_SPEW(sc->c, "RELEASE_IMAGE");

	int res = u_index_fifo_push(&sc->fifo, index);

	if (res >= 0) {
		return XRT_SUCCESS;
	}
	// FIFO full
	return XRT_ERROR_NO_IMAGE_AVAILABLE;
}


/*
 *
 * Helper functions.
 *
 */

#define D(TYPE, thing)                                                                                                 \
	if (thing != VK_NULL_HANDLE) {                                                                                 \
		vk->vkDestroy##TYPE(vk->device, thing, NULL);                                                          \
		thing = VK_NULL_HANDLE;                                                                                \
	}

static struct comp_swapchain *
alloc_and_set_funcs(struct comp_compositor *c, uint32_t num_images)
{
	struct comp_swapchain *sc = U_TYPED_CALLOC(struct comp_swapchain);
	sc->base.base.destroy = swapchain_destroy;
	sc->base.base.acquire_image = swapchain_acquire_image;
	sc->base.base.wait_image = swapchain_wait_image;
	sc->base.base.release_image = swapchain_release_image;
	sc->base.base.num_images = num_images;
	sc->c = c;

	// Make sure the handles are invalid.
	for (uint32_t i = 0; i < ARRAY_SIZE(sc->base.images); i++) {
		sc->base.images[i].handle = XRT_GRAPHICS_BUFFER_HANDLE_INVALID;
	}

	return sc;
}

static bool
is_depth_only_format(VkFormat format)
{
	return format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT;
}

static bool
is_depth_stencil_format(VkFormat format)
{

	return format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
	       format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static bool
is_stencil_only_format(VkFormat format)
{
	return format == VK_FORMAT_S8_UINT;
}

static void
do_post_create_vulkan_setup(struct comp_compositor *c,
                            const struct xrt_swapchain_create_info *info,
                            struct comp_swapchain *sc)
{
	uint32_t num_images = sc->vkic.num_images;
	VkCommandBuffer cmd_buffer;

	VkComponentMapping components = {
	    .r = VK_COMPONENT_SWIZZLE_R,
	    .g = VK_COMPONENT_SWIZZLE_G,
	    .b = VK_COMPONENT_SWIZZLE_B,
	    .a = VK_COMPONENT_SWIZZLE_ONE,
	};

	bool depth = (info->bits & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0;

	VkImageAspectFlagBits aspect = 0;
	if (depth) {
		if (is_depth_only_format(info->format)) {
			aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (is_depth_stencil_format(info->format)) {
			aspect |= VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		if (is_stencil_only_format(info->format)) {
			aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	} else {
		aspect |= VK_IMAGE_ASPECT_COLOR_BIT;
	}

	VkFormat format = info->format;
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	// Force gamma conversion for sRGB on Android
	if (format == VK_FORMAT_R8G8B8A8_SRGB) {
		format = VK_FORMAT_R8G8B8A8_UNORM;
	}
#endif

	for (uint32_t i = 0; i < num_images; i++) {
		sc->images[i].views.alpha = U_TYPED_ARRAY_CALLOC(VkImageView, info->array_size);
		sc->images[i].views.no_alpha = U_TYPED_ARRAY_CALLOC(VkImageView, info->array_size);
		sc->images[i].array_size = info->array_size;

		vk_create_sampler(&c->vk, VK_SAMPLER_ADDRESS_MODE_REPEAT, &sc->images[i].repeat_sampler);

		vk_create_sampler(&c->vk, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, &sc->images[i].sampler);


		for (uint32_t layer = 0; layer < info->array_size; ++layer) {
			VkImageSubresourceRange subresource_range = {
			    .aspectMask = aspect,
			    .baseMipLevel = 0,
			    .levelCount = 1,
			    .baseArrayLayer = layer,
			    .layerCount = 1,
			};

			vk_create_view(&c->vk, sc->vkic.images[i].handle, (VkFormat)info->format, subresource_range,
			               &sc->images[i].views.alpha[layer]);
			vk_create_view_swizzle(&c->vk, sc->vkic.images[i].handle, format, subresource_range, components,
			                       &sc->images[i].views.no_alpha[layer]);
		}
	}

	// Prime the fifo
	for (uint32_t i = 0; i < num_images; i++) {
		u_index_fifo_push(&sc->fifo, i);
	}


	/*
	 *
	 * Transition image.
	 *
	 */

	vk_init_cmd_buffer(&c->vk, &cmd_buffer);

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = aspect,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = info->array_size,
	};

	for (uint32_t i = 0; i < num_images; i++) {
		vk_set_image_layout(&c->vk, cmd_buffer, sc->vkic.images[i].handle, 0, VK_ACCESS_SHADER_READ_BIT,
		                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                    subresource_range);
	}

	vk_submit_cmd_buffer(&c->vk, cmd_buffer);
}

static void
clean_image_views(struct vk_bundle *vk, size_t array_size, VkImageView **views_ptr)
{
	VkImageView *views = *views_ptr;
	if (views == NULL) {
		return;
	}

	for (uint32_t i = 0; i < array_size; ++i) {
		if (views[i] == VK_NULL_HANDLE) {
			continue;
		}

		D(ImageView, views[i]);
	}

	free(views);
	array_size = 0;

	*views_ptr = NULL;
}

/*!
 * Free and destroy any initialized fields on the given image, safe to pass in
 * images that has one or all fields set to NULL.
 */
static void
image_cleanup(struct vk_bundle *vk, struct comp_swapchain_image *image)
{
	/*
	 * This makes sure that any pending command buffer has completed and all
	 * resources referred by it can now be manipulated. This make sure that
	 * validation doesn't complain. This is done during image destruction so
	 * isn't time critical.
	 */
	os_mutex_lock(&vk->queue_mutex);
	vk->vkDeviceWaitIdle(vk->device);
	os_mutex_unlock(&vk->queue_mutex);

	clean_image_views(vk, image->array_size, &image->views.alpha);
	clean_image_views(vk, image->array_size, &image->views.no_alpha);

	D(Sampler, image->sampler);
	D(Sampler, image->repeat_sampler);
}

/*
 *
 * Exported functions.
 *
 */

xrt_result_t
comp_swapchain_create(struct xrt_compositor *xc,
                      const struct xrt_swapchain_create_info *info,
                      struct xrt_swapchain **out_xsc)
{
	struct comp_compositor *c = comp_compositor(xc);
	uint32_t num_images = 3;
	VkResult ret;

	if (!comp_is_format_supported(c, info->format)) {
		return XRT_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	}

	if ((info->create & XRT_SWAPCHAIN_CREATE_PROTECTED_CONTENT) != 0) {
		// This compositor doesn't support creating protected content
		// swapchains.
		return XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED;
	}

	if ((info->create & XRT_SWAPCHAIN_CREATE_STATIC_IMAGE) != 0) {
		num_images = 1;
	}

	struct comp_swapchain *sc = alloc_and_set_funcs(c, num_images);

	COMP_DEBUG(c, "CREATE %p %dx%d %s", (void *)sc, //
	           info->width, info->height,           //
	           vk_color_format_string(info->format));

	// Use the image helper to allocate the images.
	ret = vk_ic_allocate(&c->vk, info, num_images, &sc->vkic);
	if (ret == VK_ERROR_FEATURE_NOT_PRESENT) {
		free(sc);
		return XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED;
	} else if (ret == VK_ERROR_FORMAT_NOT_SUPPORTED) {
		free(sc);
		return XRT_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	}
	if (ret != VK_SUCCESS) {
		free(sc);
		return XRT_ERROR_VULKAN;
	}

	xrt_graphics_buffer_handle_t handles[ARRAY_SIZE(sc->vkic.images)];

	vk_ic_get_handles(&c->vk, &sc->vkic, ARRAY_SIZE(handles), handles);
	for (uint32_t i = 0; i < sc->vkic.num_images; i++) {
		sc->base.images[i].handle = handles[i];
		sc->base.images[i].size = sc->vkic.images[i].size;
	}

	do_post_create_vulkan_setup(c, info, sc);

	*out_xsc = &sc->base.base;

	return XRT_SUCCESS;
}

xrt_result_t
comp_swapchain_import(struct xrt_compositor *xc,
                      const struct xrt_swapchain_create_info *info,
                      struct xrt_image_native *native_images,
                      uint32_t num_images,
                      struct xrt_swapchain **out_xsc)
{
	struct comp_compositor *c = comp_compositor(xc);
	VkResult ret;

	struct comp_swapchain *sc = alloc_and_set_funcs(c, num_images);

	COMP_DEBUG(c, "CREATE FROM NATIVE %p %dx%d", (void *)sc, info->width, info->height);

	// Use the image helper to get the images.
	ret = vk_ic_from_natives(&c->vk, info, native_images, num_images, &sc->vkic);
	if (ret != VK_SUCCESS) {
		return XRT_ERROR_VULKAN;
	}

	do_post_create_vulkan_setup(c, info, sc);

	*out_xsc = &sc->base.base;

	return XRT_SUCCESS;
}

void
comp_swapchain_really_destroy(struct comp_swapchain *sc)
{
	struct vk_bundle *vk = &sc->c->vk;

	COMP_SPEW(sc->c, "REALLY DESTROY");

	for (uint32_t i = 0; i < sc->base.base.num_images; i++) {
		image_cleanup(vk, &sc->images[i]);
	}

	for (uint32_t i = 0; i < sc->base.base.num_images; i++) {
		u_graphics_buffer_unref(&sc->base.images[i].handle);
	}

	vk_ic_destroy(vk, &sc->vkic);

	free(sc);
}
