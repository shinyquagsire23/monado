// Copyright 2022, Simon Zeni <simon@bl4ckb0ne.ca>
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Displays the content of one or both eye onto a desktop window
 * @author Simon Zeni <simon@bl4ckb0ne.ca>
 * @ingroup comp_main
 */

#include "main/comp_compositor.h"
#include "main/comp_renderer.h"
#include "main/comp_target_swapchain.h"
#include "main/comp_window_peek.h"

#include "util/u_debug.h"

#include <SDL2/SDL_vulkan.h>


DEBUG_GET_ONCE_OPTION(window_peek, "XRT_WINDOW_PEEK", NULL)

#define PEEK_IMAGE_USAGE (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)

#define D(TYPE, thing)                                                                                                 \
	if (thing != VK_NULL_HANDLE) {                                                                                 \
		vk->vkDestroy##TYPE(vk->device, thing, NULL);                                                          \
		thing = VK_NULL_HANDLE;                                                                                \
	}

static inline struct vk_bundle *
get_vk(struct comp_window_peek *w)
{
	return &w->c->base.vk;
}

static void *
window_peek_run_thread(void *ptr)
{
	struct comp_window_peek *w = ptr;

	w->running = true;
	w->hidden = false;
	while (w->running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT: w->running = false; break;
			case SDL_WINDOWEVENT:
				switch (event.window.event) {
				case SDL_WINDOWEVENT_HIDDEN: w->hidden = true; break;
				case SDL_WINDOWEVENT_SHOWN: w->hidden = false; break;
				case SDL_WINDOWEVENT_SIZE_CHANGED:
					w->width = event.window.data1;
					w->height = event.window.data2;
					break;
#if SDL_VERSION_ATLEAST(2, 0, 18)
				case SDL_WINDOWEVENT_DISPLAY_CHANGED:
#endif
				case SDL_WINDOWEVENT_MOVED:
					SDL_GetWindowSize(w->window, (int *)&w->width, (int *)&w->height);
					break;
				default: break;
				}
				break;
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE: w->running = false; break;
				default: break;
				}
				break;
			default: break;
			}

			if (event.type == SDL_QUIT) {
				w->running = false;
			}
		}
	}

	return NULL;
}

struct comp_window_peek *
comp_window_peek_create(struct comp_compositor *c)
{
	const char *compute = getenv("XRT_COMPOSITOR_COMPUTE");
	if (compute) {
		COMP_WARN(c, "Peek window cannot be enabled on compute compositor");
		return NULL;
	}

	const char *option = debug_get_option_window_peek();
	if (option == NULL) {
		return NULL;
	}

	struct xrt_device *xdev = c->xdev;
	enum comp_window_peek_eye eye = -1;

	int32_t width, height;
	if (strcmp(option, "both") == 0 || strcmp(option, "BOTH") == 0 || strcmp(option, "") == 0) {
		eye = COMP_WINDOW_PEEK_EYE_BOTH;
		width = xdev->hmd->screens[0].w_pixels;
		height = xdev->hmd->screens[0].h_pixels;
	} else if (strcmp(option, "left") == 0 || strcmp(option, "LEFT") == 0) {
		eye = COMP_WINDOW_PEEK_EYE_LEFT;
		width = xdev->hmd->views[0].display.w_pixels;
		height = xdev->hmd->views[0].display.h_pixels;
	} else if (strcmp(option, "right") == 0 || strcmp(option, "RIGHT") == 0) {
		eye = COMP_WINDOW_PEEK_EYE_RIGHT;
		width = xdev->hmd->views[1].display.w_pixels;
		height = xdev->hmd->views[1].display.h_pixels;
	} else {
		COMP_ERROR(c, "XRT_window_peek invalid option '%s'", option);
		COMP_ERROR(c, "must be one of 'both', 'left' or 'right'");
		return NULL;
	}

	COMP_DEBUG(c, "Creating peek window from %s eye(s)", option);

	struct comp_window_peek *w = U_TYPED_CALLOC(struct comp_window_peek);
	w->c = c;
	w->eye = eye;

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		COMP_ERROR(c, "Failed to init SDL2");
		return NULL;
	}

	int x = SDL_WINDOWPOS_UNDEFINED;
	int y = SDL_WINDOWPOS_UNDEFINED;

	int flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN;

	w->window = SDL_CreateWindow(xdev->str, x, y, width, height, flags);
	if (w->window == NULL) {
		COMP_ERROR(c, "Failed to create SDL window: %s", SDL_GetError());
		free(w);
		return NULL;
	}

	w->width = width;
	w->height = height;

	comp_target_swapchain_init_and_set_fnptrs(&w->base, COMP_TARGET_FORCE_FAKE_DISPLAY_TIMING);

	struct vk_bundle *vk = get_vk(w);

	w->base.base.name = "peek";
	w->base.base.c = c;
	w->base.display = VK_NULL_HANDLE;

	if (!SDL_Vulkan_CreateSurface(w->window, vk->instance, &w->base.surface.handle)) {
		COMP_ERROR(c, "Failed to create SDL surface: %s", SDL_GetError());
		SDL_DestroyWindow(w->window);
		free(w);
		return NULL;
	}

	/* TODO: present mode fallback to FIFO if MAILBOX is not available */
	comp_target_create_images(        //
	    &w->base.base,                //
	    w->width,                     //
	    w->height,                    //
	    w->c->settings.color_format,  //
	    w->c->settings.color_space,   //
	    PEEK_IMAGE_USAGE,             //
	    VK_PRESENT_MODE_MAILBOX_KHR); //

	VkSemaphoreCreateInfo sem_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	    .pNext = NULL,
	    .flags = 0,
	};

	VkResult ret = vk->vkCreateSemaphore(vk->device, &sem_info, NULL, &w->acquire);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}

	ret = vk->vkCreateSemaphore(vk->device, &sem_info, NULL, &w->submit);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}

	ret = vk_cmd_buffer_create(vk, &w->cmd);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "vk_cmd_buffer_create: %s", vk_result_string(ret));
	}

	os_thread_helper_init(&w->oth);
	os_thread_helper_start(&w->oth, window_peek_run_thread, w);

	return w;
}

void
comp_window_peek_destroy(struct comp_window_peek **w_ptr)
{
	struct comp_window_peek *w = *w_ptr;
	if (w == NULL) {
		return;
	}

	w->running = false;

	SDL_DestroyWindow(w->window);

	struct vk_bundle *vk = get_vk(w);

	vk->vkFreeCommandBuffers(vk->device, vk->cmd_pool, 1, &w->cmd);
	D(Semaphore, w->acquire);
	D(Semaphore, w->submit);

	comp_target_swapchain_cleanup(&w->base);
	os_thread_helper_destroy(&w->oth);

	free(w);

	*w_ptr = NULL;
}

void
comp_window_peek_blit(struct comp_window_peek *w, VkImage src, int32_t width, int32_t height)
{
	if (w->hidden) {
		return;
	}

	if (w->width != w->base.base.width || w->height != w->base.base.height) {
		COMP_DEBUG(w->c, "Resizing swapchain");
		comp_target_create_images(        //
		    &w->base.base,                //
		    w->width,                     //
		    w->height,                    //
		    w->c->settings.color_format,  //
		    w->c->settings.color_space,   //
		    PEEK_IMAGE_USAGE,             //
		    VK_PRESENT_MODE_MAILBOX_KHR); //
	}

	while (!comp_target_check_ready(&w->base.base))
		;

	uint32_t current;
	VkResult ret = comp_target_acquire(&w->base.base, w->acquire, &current);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->c, "comp_target_acquire: %s", vk_result_string(ret));
	}

	VkImage dst = w->base.base.images[current].handle;

	struct vk_bundle *vk = get_vk(w);

	// For submitting commands.
	os_mutex_lock(&vk->cmd_pool_mutex);

	VkCommandBufferBeginInfo begin_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	ret = vk->vkBeginCommandBuffer(w->cmd, &begin_info);

	VkImageSubresourceRange range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = 1,
	};

	// Barrier to make source a source
	vk_cmd_image_barrier_locked(                       //
	    vk,                                            // vk_bundle
	    w->cmd,                                        // cmdbuffer
	    src,                                           // image
	    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,          // srcAccessMask
	    VK_ACCESS_TRANSFER_READ_BIT,                   // dstAccessMask
	    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,      // oldImageLayout
	    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,          // newImageLayout
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // srcStageMask
	    VK_PIPELINE_STAGE_TRANSFER_BIT,                // dstStageMask
	    range);                                        // subresourceRange

	// Barrier to make destination a destination
	vk_cmd_image_barrier_locked(              //
	    vk,                                   // vk_bundle
	    w->cmd,                               // cmdbuffer
	    dst,                                  // image
	    0,                                    // srcAccessMask
	    VK_ACCESS_TRANSFER_WRITE_BIT,         // dstAccessMask
	    VK_IMAGE_LAYOUT_UNDEFINED,            // oldImageLayout
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // newImageLayout
	    VK_PIPELINE_STAGE_TRANSFER_BIT,       // srcStageMask
	    VK_PIPELINE_STAGE_TRANSFER_BIT,       // dstStageMask
	    range);                               // subresourceRange

	VkImageBlit blit = {
	    .srcSubresource =
	        {
	            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	            .layerCount = 1,
	        },
	    .dstSubresource =
	        {
	            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	            .layerCount = 1,
	        },
	};

	blit.srcOffsets[1].x = width;
	blit.srcOffsets[1].y = height;
	blit.srcOffsets[1].z = 1;

	blit.dstOffsets[1].x = w->width;
	blit.dstOffsets[1].y = w->height;
	blit.dstOffsets[1].z = 1;

	vk->vkCmdBlitImage(                       //
	    w->cmd,                               // commandBuffer
	    src,                                  // srcImage
	    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // srcImageLayout
	    dst,                                  // dstImage
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // dstImageLayout
	    1,                                    // regionCount
	    &blit,                                // pRegions
	    VK_FILTER_LINEAR                      // filter
	);

	// Reset destination
	vk_cmd_image_barrier_locked(              //
	    vk,                                   // vk_bundle
	    w->cmd,                               // cmdbuffer
	    dst,                                  // image
	    VK_ACCESS_TRANSFER_WRITE_BIT,         // srcAccessMask
	    0,                                    // dstAccessMask
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // oldImageLayout
	    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,      // newImageLayout
	    VK_PIPELINE_STAGE_TRANSFER_BIT,       // srcStageMask
	    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, // dstStageMask
	    range);                               // subresourceRange

	// Reset src
	vk_cmd_image_barrier_locked(                  //
	    vk,                                       // vk_bundle
	    w->cmd,                                   // cmdbuffer
	    src,                                      // image
	    VK_ACCESS_TRANSFER_READ_BIT,              // srcAccessMask
	    0,                                        // dstAccessMask
	    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,     // oldImageLayout
	    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, // newImageLayout
	    VK_PIPELINE_STAGE_TRANSFER_BIT,           // srcStageMask
	    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,        // dstStageMask
	    range);                                   // subresourceRange

	ret = vk->vkEndCommandBuffer(w->cmd);

	os_mutex_unlock(&vk->cmd_pool_mutex);

	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "Error: Could not end command buffer.\n");
		return;
	}

	VkPipelineStageFlags submit_flags = VK_PIPELINE_STAGE_TRANSFER_BIT;

	// Waits for command to finish.
	VkSubmitInfo submit = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .pNext = NULL,
	    .waitSemaphoreCount = 1,
	    .pWaitSemaphores = &w->acquire,
	    .pWaitDstStageMask = &submit_flags,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &w->cmd,
	    .signalSemaphoreCount = 1,
	    .pSignalSemaphores = &w->submit,
	};

	os_mutex_lock(&vk->queue_mutex);
	os_mutex_lock(&vk->cmd_pool_mutex);
	ret = vk->vkQueueSubmit(vk->queue, 1, &submit, VK_NULL_HANDLE);
	os_mutex_unlock(&vk->cmd_pool_mutex);
	os_mutex_unlock(&vk->queue_mutex);

	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "Error: Could not submit to queue.\n");
		return;
	}

	VkPresentInfoKHR present = {
	    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
	    .pNext = NULL,
	    .waitSemaphoreCount = 1,
	    .pWaitSemaphores = &w->submit,
	    .swapchainCount = 1,
	    .pSwapchains = &w->base.swapchain.handle,
	    .pImageIndices = &current,
	    .pResults = NULL,
	};

	os_mutex_lock(&vk->queue_mutex);
	ret = vk->vkQueuePresentKHR(vk->queue, &present);
	os_mutex_unlock(&vk->queue_mutex);

	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "Error: could not present to queue.\n");
		return;
	}
}
