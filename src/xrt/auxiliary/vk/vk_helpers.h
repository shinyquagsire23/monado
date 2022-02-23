// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common Vulkan code header.
 *
 * Note that some sections of this are generated
 * by `scripts/generate_vk_helpers.py` - lists of functions
 * and of optional extensions to check for. In those,
 * please update the script and run it, instead of editing
 * directly in this file. The generated parts are delimited
 * by special comments.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_vk
 */

#pragma once

#include "xrt/xrt_compositor.h"
#include "xrt/xrt_vulkan_includes.h"
#include "xrt/xrt_handles.h"
#include "util/u_logging.h"
#include "util/u_string_list.h"
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
	enum u_logging_level log_level;

	VkInstance instance;
	VkPhysicalDevice physical_device;
	int physical_device_index;
	VkDevice device;
	uint32_t queue_family_index;
	uint32_t queue_index;
	VkQueue queue;

	struct os_mutex queue_mutex;

	// beginning of GENERATED instance extension code - do not modify - used by scripts
	bool has_EXT_display_surface_counter;
	// end of GENERATED instance extension code - do not modify - used by scripts

	// beginning of GENERATED device extension code - do not modify - used by scripts
	bool has_KHR_timeline_semaphore;
	bool has_EXT_global_priority;
	bool has_EXT_robustness2;
	bool has_GOOGLE_display_timing;
	bool has_EXT_display_control;
	// end of GENERATED device extension code - do not modify - used by scripts

	bool is_tegra;

	//! Were timeline semaphores requested, available, and enabled?
	bool timeline_semaphores;

	VkDebugReportCallbackEXT debug_report_cb;

	VkPhysicalDeviceMemoryProperties device_memory_props;

	VkCommandPool cmd_pool;

	struct os_mutex cmd_pool_mutex;

	// Loader functions
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
	PFN_vkCreateInstance vkCreateInstance;
	PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;

	// beginning of GENERATED instance loader code - do not modify - used by scripts
	PFN_vkDestroyInstance vkDestroyInstance;
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
	PFN_vkCreateDevice vkCreateDevice;
	PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;

	PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT;
	PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT;

	PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
	PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
	PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
	PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2;
	PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
	PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
	PFN_vkGetPhysicalDeviceImageFormatProperties2 vkGetPhysicalDeviceImageFormatProperties2;

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
	PFN_vkCreateDisplayPlaneSurfaceKHR vkCreateDisplayPlaneSurfaceKHR;
	PFN_vkGetDisplayPlaneCapabilitiesKHR vkGetDisplayPlaneCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceDisplayPropertiesKHR vkGetPhysicalDeviceDisplayPropertiesKHR;
	PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR vkGetPhysicalDeviceDisplayPlanePropertiesKHR;
	PFN_vkGetDisplayModePropertiesKHR vkGetDisplayModePropertiesKHR;
	PFN_vkReleaseDisplayEXT vkReleaseDisplayEXT;

#endif // defined(VK_USE_PLATFORM_DISPLAY_KHR)

#if defined(VK_USE_PLATFORM_XCB_KHR)
	PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR;

#endif // defined(VK_USE_PLATFORM_XCB_KHR)

#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;

#endif // defined(VK_USE_PLATFORM_WAYLAND_KHR)

#if defined(VK_USE_PLATFORM_WAYLAND_KHR) && defined(VK_EXT_acquire_drm_display)
	PFN_vkAcquireDrmDisplayEXT vkAcquireDrmDisplayEXT;
	PFN_vkGetDrmDisplayEXT vkGetDrmDisplayEXT;

#endif // defined(VK_USE_PLATFORM_WAYLAND_KHR) && defined(VK_EXT_acquire_drm_display)

#if defined(VK_USE_PLATFORM_XLIB_XRANDR_EXT)
	PFN_vkGetRandROutputDisplayEXT vkGetRandROutputDisplayEXT;
	PFN_vkAcquireXlibDisplayEXT vkAcquireXlibDisplayEXT;

#endif // defined(VK_USE_PLATFORM_XLIB_XRANDR_EXT)

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;

#endif // defined(VK_USE_PLATFORM_ANDROID_KHR)

#if defined(VK_USE_PLATFORM_WIN32_KHR)
	PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;

#endif // defined(VK_USE_PLATFORM_WIN32_KHR)

#if defined(VK_EXT_display_surface_counter)
	PFN_vkGetPhysicalDeviceSurfaceCapabilities2EXT vkGetPhysicalDeviceSurfaceCapabilities2EXT;
#endif // defined(VK_EXT_display_surface_counter)

	// end of GENERATED instance loader code - do not modify - used by scripts

	// beginning of GENERATED device loader code - do not modify - used by scripts
	PFN_vkDestroyDevice vkDestroyDevice;
	PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
	PFN_vkAllocateMemory vkAllocateMemory;
	PFN_vkFreeMemory vkFreeMemory;
	PFN_vkMapMemory vkMapMemory;
	PFN_vkUnmapMemory vkUnmapMemory;

	PFN_vkCreateBuffer vkCreateBuffer;
	PFN_vkDestroyBuffer vkDestroyBuffer;
	PFN_vkBindBufferMemory vkBindBufferMemory;

	PFN_vkCreateImage vkCreateImage;
	PFN_vkDestroyImage vkDestroyImage;
	PFN_vkBindImageMemory vkBindImageMemory;

	PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
	PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
	PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
	PFN_vkGetImageMemoryRequirements2KHR vkGetImageMemoryRequirements2;
	PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayout;

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
	PFN_vkCmdBlitImage vkCmdBlitImage;
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
#if defined(VK_KHR_timeline_semaphore)
	PFN_vkSignalSemaphoreKHR vkSignalSemaphore;
	PFN_vkWaitSemaphoresKHR vkWaitSemaphores;
	PFN_vkGetSemaphoreCounterValueKHR vkGetSemaphoreCounterValue;
#endif // defined(VK_KHR_timeline_semaphore)

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

#if defined(VK_USE_PLATFORM_WIN32_KHR)
	PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR;
	PFN_vkImportSemaphoreWin32HandleKHR vkImportSemaphoreWin32HandleKHR;
	PFN_vkImportFenceWin32HandleKHR vkImportFenceWin32HandleKHR;
	PFN_vkGetFenceWin32HandleKHR vkGetFenceWin32HandleKHR;
#endif // defined(VK_USE_PLATFORM_WIN32_KHR)

#if !defined(VK_USE_PLATFORM_WIN32_KHR)
	PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR;

	PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFdKHR;
	PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR;

	PFN_vkImportFenceFdKHR vkImportFenceFdKHR;
	PFN_vkGetFenceFdKHR vkGetFenceFdKHR;
#endif // !defined(VK_USE_PLATFORM_WIN32_KHR)

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	PFN_vkGetMemoryAndroidHardwareBufferANDROID vkGetMemoryAndroidHardwareBufferANDROID;
	PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID;

#endif // defined(VK_USE_PLATFORM_ANDROID_KHR)

	PFN_vkGetPastPresentationTimingGOOGLE vkGetPastPresentationTimingGOOGLE;

#if defined(VK_EXT_display_control)
	PFN_vkGetSwapchainCounterEXT vkGetSwapchainCounterEXT;
	PFN_vkRegisterDeviceEventEXT vkRegisterDeviceEventEXT;
	PFN_vkRegisterDisplayEventEXT vkRegisterDisplayEventEXT;
#endif // defined(VK_EXT_display_control)

	// end of GENERATED device loader code - do not modify - used by scripts
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

const char *
vk_format_feature_string(VkFormatFeatureFlagBits code);

const char *
xrt_swapchain_usage_string(enum xrt_swapchain_usage_bits code);


/*
 *
 * Function and helpers.
 *
 */

#define VK_TRACE(d, ...) U_LOG_IFL_T(d->log_level, __VA_ARGS__)
#define VK_DEBUG(d, ...) U_LOG_IFL_D(d->log_level, __VA_ARGS__)
#define VK_INFO(d, ...) U_LOG_IFL_I(d->log_level, __VA_ARGS__)
#define VK_WARN(d, ...) U_LOG_IFL_W(d->log_level, __VA_ARGS__)
#define VK_ERROR(d, ...) U_LOG_IFL_E(d->log_level, __VA_ARGS__)

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
	bool timeline_semaphore;
};

/*!
 * @ingroup aux_vk
 */
struct u_string_list *
vk_build_instance_extensions(struct vk_bundle *vk,
                             struct u_string_list *required_instance_ext_list,
                             struct u_string_list *optional_instance_ext_list);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_create_device(struct vk_bundle *vk,
                 int forced_index,
                 bool only_compute,
                 VkQueueGlobalPriorityEXT global_priorty,
                 struct u_string_list *required_device_ext_list,
                 struct u_string_list *optional_device_ext_list,
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
 * @ingroup aux_vk
 * Helper to create a VkImage
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
 * Helper to create a VkImage, with more options for tiling and memory storage.
 */
VkResult
vk_create_image_advanced(struct vk_bundle *vk,
                         VkExtent3D extent,
                         VkFormat format,
                         VkImageTiling image_tiling,
                         VkImageUsageFlags image_usage_flags,
                         VkMemoryPropertyFlags memory_property_flags,
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

/*!
 * Fills in has_* in vk_bundle given a string of prefiltered instance extensions
 */
void
vk_fill_in_has_instance_extensions(struct vk_bundle *vk, struct u_string_list *ext_list);


/*
 *
 * State creation helpers, in the vk_state_creators.c file.
 *
 */

/*!
 * Arguments to @ref vk_create_descriptor_pool function.
 */
struct vk_descriptor_pool_info
{
	uint32_t uniform_per_descriptor_count;        //!< VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
	uint32_t sampler_per_descriptor_count;        //!< VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
	uint32_t storage_image_per_descriptor_count;  //!< VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
	uint32_t storage_buffer_per_descriptor_count; //!< VK_DESCRIPTOR_TYPE_STORAGE_BUFFER

	//! The max count of created descriptors.
	uint32_t descriptor_count;

	//! Are descriptors freeable, or must vkResetDescriptorPool be used.
	bool freeable;
};

/*!
 * Creates a descriptor pool, made for a single layout.
 *
 * Does error logging.
 */
VkResult
vk_create_descriptor_pool(struct vk_bundle *vk,
                          const struct vk_descriptor_pool_info *info,
                          VkDescriptorPool *out_descriptor_pool);

/*!
 * Creates a descriptor set.
 *
 * Does error logging.
 */
VkResult
vk_create_descriptor_set(struct vk_bundle *vk,
                         VkDescriptorPool descriptor_pool,
                         VkDescriptorSetLayout descriptor_layout,
                         VkDescriptorSet *out_descriptor_set);

/*!
 * Creates a pipeline layout from a single descriptor set layout.
 *
 * Does error logging.
 */
VkResult
vk_create_pipeline_layout(struct vk_bundle *vk,
                          VkDescriptorSetLayout descriptor_set_layout,
                          VkPipelineLayout *out_pipeline_layout);

/*!
 * Creates a pipeline cache.
 *
 * Does error logging.
 */
VkResult
vk_create_pipeline_cache(struct vk_bundle *vk, VkPipelineCache *out_pipeline_cache);

/*!
 * Creates a compute pipeline, assumes entry function is called 'main'.
 *
 * Does error logging.
 */
VkResult
vk_create_compute_pipeline(struct vk_bundle *vk,
                           VkPipelineCache pipeline_cache,
                           VkShaderModule shader,
                           VkPipelineLayout pipeline_layout,
                           VkPipeline *out_compute_pipeline);


/*
 *
 * Command buffer helpers, in the vk_command_buffer.c file.
 *
 */

/*!
 * Creates a new command buffer using the bundle's pool, takes the cmd_pool_mutex.
 *
 * Does error logging.
 */
VkResult
vk_create_command_buffer(struct vk_bundle *vk, VkCommandBuffer *out_cmd);

/*!
 * Destroys a command buffer, takes the cmd_pool_mutex.
 *
 * Does error logging.
 */
void
vk_destroy_command_buffer(struct vk_bundle *vk, VkCommandBuffer command_buffer);

/*!
 * Issues the vkBeginCommandBuffer function on the command buffer.
 *
 * Does error logging.
 */
VkResult
vk_begin_command_buffer(struct vk_bundle *vk, VkCommandBuffer command_buffer);

/*!
 * Issues the vkEndCommandBuffer function on the command buffer.
 *
 * Does error logging.
 */
VkResult
vk_end_command_buffer(struct vk_bundle *vk, VkCommandBuffer command_buffer);

/*
 *
 * Compositor swapchain image flags helpers, in the vk_compositor_flags.c file.
 *
 */

/*!
 * Returns the access flags for the compositor to app barriers.
 *
 * CSCI = Compositor SwapChain Images.
 */
VkAccessFlags
vk_csci_get_barrier_access_mask(enum xrt_swapchain_usage_bits bits);

/*!
 * Return the optimal layout for this format, this is the layout as given to the
 * app so is bound to the OpenXR spec.
 *
 * CSCI = Compositor SwapChain Images.
 */
VkImageLayout
vk_csci_get_barrier_optimal_layout(VkFormat format);

/*!
 * Return the barrier aspect mask for this format, this is intended for the
 * barriers that flush the data out before and after transfers between the
 * application and compositor.
 *
 * CSCI = Compositor SwapChain Images.
 */
VkImageAspectFlags
vk_csci_get_barrier_aspect_mask(VkFormat format);

/*!
 * Returns the usage bits for a given selected format and usage.
 *
 * For color formats always adds:
 * * `VK_IMAGE_USAGE_SAMPLED_BIT` for compositor reading in shaders.
 *
 * For depth & stencil formats always adds:
 * * `VK_IMAGE_USAGE_SAMPLED_BIT` for compositor reading in shaders.
 *
 * For depth formats always adds:
 * * `VK_IMAGE_USAGE_SAMPLED_BIT` for compositor reading in shaders.
 *
 * For stencil formats always adds:
 * * `VK_IMAGE_USAGE_SAMPLED_BIT` for compositor reading in shaders.
 *
 * CSCI = Compositor SwapChain Images.
 */
VkImageUsageFlags
vk_csci_get_image_usage_flags(struct vk_bundle *vk, VkFormat format, enum xrt_swapchain_usage_bits bits);

/*!
 * For images views created by the compositor to sample the images, what aspect
 * should be set. For color it's the color, for depth and stencil it's only
 * depth as both are disallowed by the Vulkan spec, for depth only depth, and
 * for stencil only it's stencil.
 *
 * CSCI = Compositor SwapChain Images.
 */
VkImageAspectFlags
vk_csci_get_image_view_aspect(VkFormat format, enum xrt_swapchain_usage_bits bits);


/*!
 *
 * Adds barrier to image
 *
 */
void
vk_insert_image_memory_barrier(struct vk_bundle *vk,
                               VkCommandBuffer cmdbuffer,
                               VkImage image,
                               VkAccessFlags srcAccessMask,
                               VkAccessFlags dstAccessMask,
                               VkImageLayout oldImageLayout,
                               VkImageLayout newImageLayout,
                               VkPipelineStageFlags srcStageMask,
                               VkPipelineStageFlags dstStageMask,
                               VkImageSubresourceRange subresourceRange);



/*
 *
 * Sync objects, in the vk_sync_objects.c file.
 *
 */

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


#ifdef __cplusplus
}
#endif
