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
#include "xrt/xrt_handles.h"
#include "util/u_logging.h"
#include "os/os_threading.h"

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
	enum u_logging_level ll;

	VkInstance instance;
	VkPhysicalDevice physical_device;
	int physical_device_index;
	VkDevice device;
	uint32_t queue_family_index;
	uint32_t queue_index;
	VkQueue queue;

	struct os_mutex queue_mutex;

	bool has_GOOGLE_display_timing;
	bool has_EXT_global_priority;
	bool has_VK_EXT_robustness2;

	bool is_tegra;

	VkDebugReportCallbackEXT debug_report_cb;

	VkPhysicalDeviceMemoryProperties device_memory_props;

	VkCommandPool cmd_pool;

	struct os_mutex cmd_pool_mutex;

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
	PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
	PFN_vkCreateDisplayPlaneSurfaceKHR vkCreateDisplayPlaneSurfaceKHR;
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
	PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR;
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
#ifdef VK_EXT_acquire_drm_display
	PFN_vkAcquireDrmDisplayEXT vkAcquireDrmDisplayEXT;
	PFN_vkGetDrmDisplayEXT vkGetDrmDisplayEXT;
#endif
#endif

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
	PFN_vkAcquireXlibDisplayEXT vkAcquireXlibDisplayEXT;
	PFN_vkGetRandROutputDisplayEXT vkGetRandROutputDisplayEXT;
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
	PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;
#endif

#ifdef VK_USE_PLATFORM_WIN32_KHR
	PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
#endif

	// Physical device functions.
	PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
	PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
	PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
	PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2;

	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;

	PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;

	PFN_vkGetPhysicalDeviceImageFormatProperties2 vkGetPhysicalDeviceImageFormatProperties2;

#ifdef VK_USE_PLATFORM_DISPLAY_KHR
	PFN_vkGetDisplayModePropertiesKHR vkGetDisplayModePropertiesKHR;
	PFN_vkGetPhysicalDeviceDisplayPropertiesKHR vkGetPhysicalDeviceDisplayPropertiesKHR;
	PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR vkGetPhysicalDeviceDisplayPlanePropertiesKHR;
	PFN_vkGetDisplayPlaneCapabilitiesKHR vkGetDisplayPlaneCapabilitiesKHR;
	PFN_vkReleaseDisplayEXT vkReleaseDisplayEXT;
#endif

	// Device functions.
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
	PFN_vkDestroyDevice vkDestroyDevice;
	PFN_vkDeviceWaitIdle vkDeviceWaitIdle;

	PFN_vkAllocateMemory vkAllocateMemory;
	PFN_vkFreeMemory vkFreeMemory;
	PFN_vkMapMemory vkMapMemory;
	PFN_vkUnmapMemory vkUnmapMemory;
	PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR;

#ifdef VK_USE_PLATFORM_ANDROID_KHR
	PFN_vkGetMemoryAndroidHardwareBufferANDROID vkGetMemoryAndroidHardwareBufferANDROID;
	PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID;
#endif

#ifdef VK_USE_PLATFORM_WIN32_KHR
	PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR;
#endif

	PFN_vkCreateBuffer vkCreateBuffer;
	PFN_vkDestroyBuffer vkDestroyBuffer;
	PFN_vkBindBufferMemory vkBindBufferMemory;
	PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
	PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;

	PFN_vkCreateImage vkCreateImage;
	PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
	PFN_vkGetImageMemoryRequirements2 vkGetImageMemoryRequirements2;
	PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayout;
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
	PFN_vkCmdDispatch vkCmdDispatch;
	PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
	PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
	PFN_vkCmdCopyImage vkCmdCopyImage;
	PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
	PFN_vkEndCommandBuffer vkEndCommandBuffer;
	PFN_vkFreeCommandBuffers vkFreeCommandBuffers;

	PFN_vkCreateRenderPass vkCreateRenderPass;
	PFN_vkDestroyRenderPass vkDestroyRenderPass;
	PFN_vkCreateFramebuffer vkCreateFramebuffer;
	PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
	PFN_vkCreatePipelineCache vkCreatePipelineCache;
	PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
	PFN_vkResetDescriptorPool vkResetDescriptorPool;
	PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
	PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
	PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
	PFN_vkFreeDescriptorSets vkFreeDescriptorSets;
	PFN_vkCreateComputePipelines vkCreateComputePipelines;
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
#ifdef VK_KHR_timeline_semaphore
	PFN_vkSignalSemaphoreKHR vkSignalSemaphore;
#endif
	PFN_vkDestroySemaphore vkDestroySemaphore;

	PFN_vkCreateFence vkCreateFence;
	PFN_vkWaitForFences vkWaitForFences;
	PFN_vkGetFenceStatus vkGetFenceStatus;
	PFN_vkDestroyFence vkDestroyFence;
	PFN_vkResetFences vkResetFences;

	PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
	PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
	PFN_vkQueuePresentKHR vkQueuePresentKHR;

#ifdef VK_USE_PLATFORM_WIN32_KHR
	PFN_vkImportSemaphoreWin32HandleKHR vkImportSemaphoreWin32HandleKHR;
	PFN_vkImportFenceWin32HandleKHR vkImportFenceWin32HandleKHR;
#else
	PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFdKHR;
	PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR;

	PFN_vkImportFenceFdKHR vkImportFenceFdKHR;
	PFN_vkGetFenceFdKHR vkGetFenceFdKHR;
#endif

	PFN_vkGetPastPresentationTimingGOOGLE vkGetPastPresentationTimingGOOGLE;
	// clang-format on
};

struct vk_buffer
{
	VkBuffer handle;
	VkDeviceMemory memory;
	uint32_t size;
	void *data;
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

#define VK_TRACE(d, ...) U_LOG_IFL_T(d->ll, __VA_ARGS__)
#define VK_DEBUG(d, ...) U_LOG_IFL_D(d->ll, __VA_ARGS__)
#define VK_INFO(d, ...) U_LOG_IFL_I(d->ll, __VA_ARGS__)
#define VK_WARN(d, ...) U_LOG_IFL_W(d->ll, __VA_ARGS__)
#define VK_ERROR(d, ...) U_LOG_IFL_E(d->ll, __VA_ARGS__)

/*!
 * @brief Check a Vulkan VkResult, writing an error to the log and returning true if not VK_SUCCESS
 *
 * @param fun a string literal with the name of the Vulkan function, for logging purposes.
 * @param res a VkResult from that function.
 * @param file a string literal with the source code filename, such as from __FILE__
 * @param line a source code line number, such as from __LINE__
 *
 * @see vk_check_error, vk_check_error_with_free which wrap this for easier usage.
 *
 * @ingroup aux_vk
 */
bool
vk_has_error(VkResult res, const char *fun, const char *file, int line);

/*!
 * @def vk_check_error
 * @brief Perform checking of a Vulkan result, returning in case it is not VK_SUCCESS.
 *
 * @param fun A string literal with the name of the Vulkan function, for logging purposes.
 * @param res a VkResult from that function.
 * @param ret value to return, if any, upon error
 *
 * @see vk_has_error which is wrapped by this macro
 *
 * @ingroup aux_vk
 */
#define vk_check_error(fun, res, ret)                                                                                  \
	do {                                                                                                           \
		if (vk_has_error(res, fun, __FILE__, __LINE__))                                                        \
			return ret;                                                                                    \
	} while (0)

/*!
 * @def vk_check_error_with_free
 * @brief Perform checking of a Vulkan result, freeing an allocation and returning in case it is not VK_SUCCESS.
 *
 * @param fun A string literal with the name of the Vulkan function, for logging purposes.
 * @param res a VkResult from that function.
 * @param ret value to return, if any, upon error
 * @param to_free expression to pass to `free()` upon error
 *
 * @see vk_has_error which is wrapped by this macro
 *
 * @ingroup aux_vk
 */
#define vk_check_error_with_free(fun, res, ret, to_free)                                                               \
	do {                                                                                                           \
		if (vk_has_error(res, fun, __FILE__, __LINE__)) {                                                      \
			free(to_free);                                                                                 \
			return ret;                                                                                    \
		}                                                                                                      \
	} while (0)

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
 * @brief Initialize mutexes in the @ref vk_bundle.
 *
 * Not required for all uses, but a precondition for some.
 *
 * @ingroup aux_vk
 */
VkResult
vk_init_mutex(struct vk_bundle *vk);

/*!
 * @brief De-initialize mutexes in the @ref vk_bundle.
 * @ingroup aux_vk
 */
VkResult
vk_deinit_mutex(struct vk_bundle *vk);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_init_cmd_pool(struct vk_bundle *vk);

/*!
 * Used to enable device features as a argument @ref vk_create_device.
 *
 * @ingroup aux_vk
 */
struct vk_device_features
{
	bool shader_storage_image_write_without_format;
	bool null_descriptor;
};

/*!
 * @ingroup aux_vk
 */
VkResult
vk_create_device(struct vk_bundle *vk,
                 int forced_index,
                 bool only_compute,
                 VkQueueGlobalPriorityEXT global_priorty,
                 const char *const *required_device_extensions,
                 size_t num_required_device_extensions,
                 const char *const *optional_device_extensions,
                 size_t num_optional_device_extension,
                 const struct vk_device_features *optional_device_features);

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
vk_get_memory_type(struct vk_bundle *vk, uint32_t type_bits, VkMemoryPropertyFlags memory_props, uint32_t *out_type_id);

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
 *
 * @brief Creates a Vulkan device memory and image from a native graphics buffer handle.
 *
 * In case of error, ownership is never transferred and the caller should close the handle themselves.
 *
 * In case of success, the underlying Vulkan functionality's ownership semantics apply: ownership of the @p image_native
 * handle may have transferred, a reference may have been added, or the Vulkan objects may rely on the caller to keep
 * the native handle alive until the Vulkan objects are destroyed. Which option applies depends on the particular native
 * handle type used.
 *
 * See the corresponding specification texts:
 *
 * - Windows:
 *   https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VkImportMemoryWin32HandleInfoKHR
 * - Linux: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VkImportMemoryFdInfoKHR
 * - Android:
 *   https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VkImportAndroidHardwareBufferInfoANDROID
 *
 * @ingroup aux_vk
 */
VkResult
vk_create_image_from_native(struct vk_bundle *vk,
                            const struct xrt_swapchain_create_info *info,
                            struct xrt_image_native *image_native,
                            VkImage *out_image,
                            VkDeviceMemory *out_mem);

/*!
 * @brief Creates a Vulkan fence from a native graphics sync handle.
 *
 * In case of error, ownership is never transferred and the caller should close the handle themselves.
 *
 * In case of success, the underlying Vulkan functionality's ownership semantics apply: ownership of the @p native
 * handle may have transferred, a reference may have been added, or the Vulkan object may rely on the caller to keep the
 * native handle alive until the Vulkan object is destroyed. Which option applies depends on the particular native
 * handle type used.
 *
 * See the corresponding Vulkan specification text:
 * https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#synchronization-fences-importing
 *
 * @ingroup aux_vk
 */
VkResult
vk_create_fence_sync_from_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t native, VkFence *out_fence);

/*!
 * @brief Creates a Vulkan semaphore from a native graphics sync handle.
 *
 * In case of error, ownership is never transferred and the caller should close the handle themselves.
 *
 * In case of success, the underlying Vulkan functionality's ownership semantics apply: ownership of the @p native
 * handle may have transferred, a reference may have been added, or the Vulkan object may rely on the caller to keep the
 * native handle alive until the Vulkan object is destroyed. Which option applies depends on the particular native
 * handle type used.
 *
 * @ingroup aux_vk
 */
VkResult
vk_create_semaphore_from_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t native, VkSemaphore *out_sem);

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
vk_create_sampler(struct vk_bundle *vk, VkSamplerAddressMode clamp_mode, VkSampler *out_sampler);

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
vk_create_view_swizzle(struct vk_bundle *vk,
                       VkImage image,
                       VkFormat format,
                       VkImageSubresourceRange subresource_range,
                       VkComponentMapping components,
                       VkImageView *out_view);

/*!
 * @pre Requires successful call to vk_init_mutex
 * @ingroup aux_vk
 */
VkResult
vk_init_cmd_buffer(struct vk_bundle *vk, VkCommandBuffer *out_cmd_buffer);

/*!
 * @pre Requires successful call to vk_init_mutex
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
 * @pre Requires successful call to vk_init_mutex
 * @ingroup aux_vk
 */
VkResult
vk_submit_cmd_buffer(struct vk_bundle *vk, VkCommandBuffer cmd_buffer);


VkAccessFlags
vk_get_access_flags(VkImageLayout layout);

VkAccessFlags
vk_swapchain_access_flags(enum xrt_swapchain_usage_bits bits);

/*!
 * Always adds `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` and
 * `VK_IMAGE_USAGE_SAMPLED_BIT` to color formats so they can be used by the
 * compositor and client.
 */
VkImageUsageFlags
vk_swapchain_usage_flags(struct vk_bundle *vk, VkFormat format, enum xrt_swapchain_usage_bits bits);

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

bool
vk_update_buffer(struct vk_bundle *vk, float *buffer, size_t buffer_size, VkDeviceMemory memory);

/*!
 * @pre Requires successful call to vk_init_mutex
 * @ingroup aux_vk
 */
VkResult
vk_locked_submit(struct vk_bundle *vk, VkQueue queue, uint32_t count, const VkSubmitInfo *infos, VkFence fence);

#ifdef __cplusplus
}
#endif
