// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common Vulkan code header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup aux_vk
 */

#pragma once

#include "xrt/xrt_compositor.h"
#include "xrt/xrt_vulkan_includes.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Structs
 *
 */

/*!
 * A bundle of Vulkan functions and objects, used by both @ref comp and @ref
 * comp_client. Note that they both have different instances of the object and
 * as such VkInstance and so on.
 *
 * @ingroup aux_vk
 */
struct vk_bundle
{
	bool print;

	VkInstance instance;
	VkPhysicalDevice physical_device;
	VkDevice device;
	uint32_t queue_family_index;
	uint32_t queue_index;
	VkQueue queue;

	VkDebugReportCallbackEXT debug_report_cb;

	VkPhysicalDeviceMemoryProperties device_memory_props;

	VkCommandPool cmd_pool;

	// clang-format off
	// Loader functions
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
	PFN_vkCreateInstance vkCreateInstance;

	// Instance functions.
	PFN_vkDestroyInstance vkDestroyInstance;
	PFN_vkCreateDevice vkCreateDevice;
	PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT;
	PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT;
	PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
	PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;

#ifdef VK_USE_PLATFORM_XCB_KHR
	PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR;
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
#endif

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
	PFN_vkCreateDisplayPlaneSurfaceKHR vkCreateDisplayPlaneSurfaceKHR;
	PFN_vkGetDisplayPlaneCapabilitiesKHR vkGetDisplayPlaneCapabilitiesKHR;

	// This doesn't strictly require VK_USE_PLATFORM_XLIB_XRANDR_EXT,
	// but it's only used in the NVIDIA X direct mode path that does require it.
	PFN_vkGetPhysicalDeviceDisplayPropertiesKHR vkGetPhysicalDeviceDisplayPropertiesKHR;
	PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR vkGetPhysicalDeviceDisplayPlanePropertiesKHR;
	PFN_vkGetDisplayModePropertiesKHR vkGetDisplayModePropertiesKHR;
	PFN_vkAcquireXlibDisplayEXT vkAcquireXlibDisplayEXT;
	PFN_vkReleaseDisplayEXT vkReleaseDisplayEXT;
	PFN_vkGetRandROutputDisplayEXT vkGetRandROutputDisplayEXT;
#endif


	// Physical device functions.
	PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
	PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;

	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;


	// Device functions.
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
	PFN_vkDestroyDevice vkDestroyDevice;
	PFN_vkDeviceWaitIdle vkDeviceWaitIdle;

	PFN_vkAllocateMemory vkAllocateMemory;
	PFN_vkFreeMemory vkFreeMemory;
	PFN_vkMapMemory vkMapMemory;
	PFN_vkUnmapMemory vkUnmapMemory;
	PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR;

	PFN_vkCreateBuffer vkCreateBuffer;
	PFN_vkDestroyBuffer vkDestroyBuffer;
	PFN_vkBindBufferMemory vkBindBufferMemory;
	PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
	PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;

	PFN_vkCreateImage vkCreateImage;
	PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
	PFN_vkBindImageMemory vkBindImageMemory;
	PFN_vkDestroyImage vkDestroyImage;
	PFN_vkCreateImageView vkCreateImageView;
	PFN_vkDestroyImageView vkDestroyImageView;

	PFN_vkCreateSampler vkCreateSampler;
	PFN_vkDestroySampler vkDestroySampler;

	PFN_vkCreateShaderModule vkCreateShaderModule;
	PFN_vkDestroyShaderModule vkDestroyShaderModule;

	PFN_vkCreateCommandPool vkCreateCommandPool;
	PFN_vkDestroyCommandPool vkDestroyCommandPool;
	PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
	PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
	PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
	PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
	PFN_vkCmdSetScissor vkCmdSetScissor;
	PFN_vkCmdSetViewport vkCmdSetViewport;
	PFN_vkCmdClearColorImage vkCmdClearColorImage;
	PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
	PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
	PFN_vkCmdBindPipeline vkCmdBindPipeline;
	PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
	PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
	PFN_vkCmdDraw vkCmdDraw;
	PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
	PFN_vkEndCommandBuffer vkEndCommandBuffer;
	PFN_vkFreeCommandBuffers vkFreeCommandBuffers;

	PFN_vkCreateRenderPass vkCreateRenderPass;
	PFN_vkDestroyRenderPass vkDestroyRenderPass;
	PFN_vkCreateFramebuffer vkCreateFramebuffer;
	PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
	PFN_vkCreatePipelineCache vkCreatePipelineCache;
	PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
	PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
	PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
	PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
	PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
	PFN_vkDestroyPipeline vkDestroyPipeline;
	PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
	PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
	PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
	PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
	PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;

	PFN_vkGetDeviceQueue vkGetDeviceQueue;
	PFN_vkQueueSubmit vkQueueSubmit;
	PFN_vkQueueWaitIdle vkQueueWaitIdle;

	PFN_vkCreateSemaphore vkCreateSemaphore;
	PFN_vkDestroySemaphore vkDestroySemaphore;

	PFN_vkCreateFence vkCreateFence;
	PFN_vkWaitForFences vkWaitForFences;
	PFN_vkDestroyFence vkDestroyFence;
	PFN_vkResetFences vkResetFences;

	PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
	PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
	PFN_vkQueuePresentKHR vkQueuePresentKHR;

	PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFdKHR;
	PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR;
	// clang-format on
};

struct vk_buffer
{
	VkBuffer handle;
	VkDeviceMemory memory;
	uint32_t size;
	void *data;
};

struct vk_image
{
	VkImage handle;
	VkDeviceMemory memory;
	VkImageView view;
	VkSampler sampler;
};

/*
 *
 * String helper functions.
 *
 */

const char *
vk_result_string(VkResult code);

const char *
vk_color_format_string(VkFormat code);

const char *
vk_present_mode_string(VkPresentModeKHR code);

const char *
vk_power_state_string(VkDisplayPowerStateEXT code);

const char *
vk_color_space_string(VkColorSpaceKHR code);


/*
 *
 * Function and helpers.
 *
 */

#define VK_DEBUG(vk, ...)                                                      \
	do {                                                                   \
		if (vk->print) {                                               \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define VK_ERROR(vk, ...)                                                      \
	do {                                                                   \
		fprintf(stderr, "%s - ", __func__);                            \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, "\n");                                         \
	} while (false)

bool
vk_has_error(VkResult res, const char *fun, const char *file, int line);

#define vk_check_error(fun, res, ret)                                          \
	if (vk_has_error(res, fun, __FILE__, __LINE__))                        \
	return ret

/*!
 * @ingroup aux_vk
 */
VkResult
vk_get_loader_functions(struct vk_bundle *vk, PFN_vkGetInstanceProcAddr g);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_get_instance_functions(struct vk_bundle *vk);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_init_cmd_pool(struct vk_bundle *vk);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_create_device(struct vk_bundle *vk, int forced_index);

/*!
 * Initialize a bundle with objects given to us by client code,
 * used by @ref client_vk_compositor in @ref comp_client.
 *
 * @ingroup aux_vk
 */
VkResult
vk_init_from_given(struct vk_bundle *vk,
                   PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr,
                   VkInstance instance,
                   VkPhysicalDevice physical_device,
                   VkDevice device,
                   uint32_t queue_family_index,
                   uint32_t queue_index);

/*!
 * @ingroup aux_vk
 */
bool
vk_get_memory_type(struct vk_bundle *vk,
                   uint32_t type_bits,
                   VkMemoryPropertyFlags memory_props,
                   uint32_t *out_type_id);

/*!
 * Allocate memory for an image and bind it to that image.
 *
 * Handles the following steps:
 *
 * - calling vkGetImageMemoryRequirements
 *   - comparing against the max_size
 * - getting the memory type (as dictated by the VkMemoryRequirements and
 *   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
 * - calling vkAllocateMemory
 * - calling vkBindImageMemory
 * - calling vkDestroyMemory in case of an error.
 *
 * If this fails, it cleans up the VkDeviceMemory.
 *
 * @param vk Vulkan bundle
 * @param image The VkImage to allocate for and bind.
 * @param max_size The maximum value you'll allow for
 *        VkMemoryRequirements::size. Pass SIZE_MAX if you will accept any size
 *        that works.
 * @param pNext_for_allocate (Optional) a pointer to use in the pNext chain of
 *        VkMemoryAllocateInfo.
 * @param out_mem Output parameter: will be set to the allocated memory if
 *        everything succeeds. Not modified if there is an error.
 * @param out_size (Optional) pointer to receive the value of
 *        VkMemoryRequirements::size.
 *
 * If this fails, you may want to destroy your VkImage as well, since this
 * routine is usually used in combination with vkCreateImage.
 *
 * @ingroup aux_vk
 */
VkResult
vk_alloc_and_bind_image_memory(struct vk_bundle *vk,
                               VkImage image,
                               size_t max_size,
                               const void *pNext_for_allocate,
                               VkDeviceMemory *out_mem,
                               VkDeviceSize *out_size);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_create_image_from_fd(struct vk_bundle *vk,
                        enum xrt_swapchain_usage_bits swapchain_usage,
                        int64_t format,
                        uint32_t width,
                        uint32_t height,
                        uint32_t array_size,
                        uint32_t mip_count,
                        struct xrt_image_fd *image_fd,
                        VkImage *out_image,
                        VkDeviceMemory *out_mem);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_create_semaphore_from_fd(struct vk_bundle *vk, int fd, VkSemaphore *out_sem);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_create_image_simple(struct vk_bundle *vk,
                       VkExtent2D extent,
                       VkFormat format,
                       VkImageUsageFlags usage,
                       VkDeviceMemory *out_mem,
                       VkImage *out_image);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_create_sampler(struct vk_bundle *vk, VkSampler *out_sampler);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_create_view(struct vk_bundle *vk,
               VkImage image,
               VkFormat format,
               VkImageSubresourceRange subresource_range,
               VkImageView *out_view);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_init_cmd_buffer(struct vk_bundle *vk, VkCommandBuffer *out_cmd_buffer);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_set_image_layout(struct vk_bundle *vk,
                    VkCommandBuffer cmd_buffer,
                    VkImage image,
                    VkAccessFlags src_access_mask,
                    VkAccessFlags dst_access_mask,
                    VkImageLayout old_layout,
                    VkImageLayout new_layout,
                    VkImageSubresourceRange subresource_range);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_submit_cmd_buffer(struct vk_bundle *vk, VkCommandBuffer cmd_buffer);


VkAccessFlags
vk_get_access_flags(VkImageLayout layout);

bool
vk_init_descriptor_pool(struct vk_bundle *vk,
                        const VkDescriptorPoolSize *pool_sizes,
                        uint32_t pool_size_count,
                        uint32_t set_count,
                        VkDescriptorPool *out_descriptor_pool);

bool
vk_allocate_descriptor_sets(struct vk_bundle *vk,
                            VkDescriptorPool descriptor_pool,
                            uint32_t count,
                            const VkDescriptorSetLayout *set_layout,
                            VkDescriptorSet *sets);

bool
vk_buffer_init(struct vk_bundle *vk,
               VkDeviceSize size,
               VkBufferUsageFlags usage,
               VkMemoryPropertyFlags properties,
               VkBuffer *out_buffer,
               VkDeviceMemory *out_mem);

void
vk_buffer_destroy(struct vk_buffer *self, struct vk_bundle *vk);


void
vk_image_destroy(struct vk_image *self, struct vk_bundle *vk);

#ifdef __cplusplus
}
#endif
