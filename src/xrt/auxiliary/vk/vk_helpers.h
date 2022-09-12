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

#include "xrt/xrt_compiler.h"
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
 * comp_client. Note that they both have different instances of the object, and
 * thus different VkInstance, etc.
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

	struct
	{
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)
		bool color_image_import_opaque_win32;
		bool color_image_export_opaque_win32;
		bool depth_image_import_opaque_win32;
		bool depth_image_export_opaque_win32;

		bool color_image_import_d3d11;
		bool color_image_export_d3d11;
		bool depth_image_import_d3d11;
		bool depth_image_export_d3d11;

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
		bool color_image_import_opaque_fd;
		bool color_image_export_opaque_fd;
		bool depth_image_import_opaque_fd;
		bool depth_image_export_opaque_fd;

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
		bool color_image_import_opaque_fd;
		bool color_image_export_opaque_fd;
		bool depth_image_import_opaque_fd;
		bool depth_image_export_opaque_fd;

		bool color_image_import_ahardwarebuffer;
		bool color_image_export_ahardwarebuffer;
		bool depth_image_import_ahardwarebuffer;
		bool depth_image_export_ahardwarebuffer;
#endif

#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
		bool fence_sync_fd;
		bool fence_opaque_fd;

		bool binary_semaphore_sync_fd;
		bool binary_semaphore_opaque_fd;

		bool timeline_semaphore_sync_fd;
		bool timeline_semaphore_opaque_fd;
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
		bool fence_win32_handle;

		bool binary_semaphore_d3d12_fence;
		bool binary_semaphore_win32_handle;

		bool timeline_semaphore_d3d12_fence;
		bool timeline_semaphore_win32_handle;
#else
#error "Need port for fence sync handles checkers"
#endif
	} external;

	// beginning of GENERATED instance extension code - do not modify - used by scripts
	bool has_EXT_display_surface_counter;
	// end of GENERATED instance extension code - do not modify - used by scripts

	// beginning of GENERATED device extension code - do not modify - used by scripts
	bool has_KHR_external_fence_fd;
	bool has_KHR_external_semaphore_fd;
	bool has_KHR_image_format_list;
	bool has_KHR_maintenance1;
	bool has_KHR_maintenance2;
	bool has_KHR_maintenance3;
	bool has_KHR_maintenance4;
	bool has_KHR_timeline_semaphore;
	bool has_EXT_calibrated_timestamps;
	bool has_EXT_display_control;
	bool has_EXT_global_priority;
	bool has_EXT_robustness2;
	bool has_GOOGLE_display_timing;
	// end of GENERATED device extension code - do not modify - used by scripts

	struct
	{
		//! Are timestamps available for compute and graphics queues?
		bool timestamp_compute_and_graphics;

		//! Nanoseconds per gpu tick.
		float timestamp_period;

		//! Valid bits in the queue selected.
		uint32_t timestamp_valid_bits;

		//! Were timeline semaphore requested, available, and enabled?
		bool timeline_semaphore;

		//! Per stage limit on sampled images (includes combined).
		uint32_t max_per_stage_descriptor_sampled_images;

		//! Per stage limit on storage images.
		uint32_t max_per_stage_descriptor_storage_images;
	} features;

	//! Is the GPU a tegra device.
	bool is_tegra;


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
	PFN_vkGetPhysicalDeviceImageFormatProperties2 vkGetPhysicalDeviceImageFormatProperties2;
	PFN_vkGetPhysicalDeviceExternalBufferPropertiesKHR vkGetPhysicalDeviceExternalBufferPropertiesKHR;
	PFN_vkGetPhysicalDeviceExternalFencePropertiesKHR vkGetPhysicalDeviceExternalFencePropertiesKHR;
	PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR vkGetPhysicalDeviceExternalSemaphorePropertiesKHR;
	PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
	PFN_vkEnumerateDeviceLayerProperties vkEnumerateDeviceLayerProperties;

#if defined(VK_EXT_calibrated_timestamps)
	PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT vkGetPhysicalDeviceCalibrateableTimeDomainsEXT;

#endif // defined(VK_EXT_calibrated_timestamps)

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

	PFN_vkCreateQueryPool vkCreateQueryPool;
	PFN_vkDestroyQueryPool vkDestroyQueryPool;
	PFN_vkGetQueryPoolResults vkGetQueryPoolResults;

	PFN_vkCreateCommandPool vkCreateCommandPool;
	PFN_vkDestroyCommandPool vkDestroyCommandPool;
	PFN_vkResetCommandPool vkResetCommandPool;

	PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
	PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
	PFN_vkCmdBeginQuery vkCmdBeginQuery;
	PFN_vkCmdCopyQueryPoolResults vkCmdCopyQueryPoolResults;
	PFN_vkCmdEndQuery vkCmdEndQuery;
	PFN_vkCmdResetQueryPool vkCmdResetQueryPool;
	PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp;
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
	PFN_vkGetFenceWin32HandleKHR vkGetFenceWin32HandleKHR;
	PFN_vkGetSemaphoreWin32HandleKHR vkGetSemaphoreWin32HandleKHR;
	PFN_vkImportFenceWin32HandleKHR vkImportFenceWin32HandleKHR;
	PFN_vkImportSemaphoreWin32HandleKHR vkImportSemaphoreWin32HandleKHR;

#endif // defined(VK_USE_PLATFORM_WIN32_KHR)

#if !defined(VK_USE_PLATFORM_WIN32_KHR)
	PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR;
	PFN_vkGetFenceFdKHR vkGetFenceFdKHR;
	PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR;
	PFN_vkImportFenceFdKHR vkImportFenceFdKHR;
	PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFdKHR;

#endif // !defined(VK_USE_PLATFORM_WIN32_KHR)

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	PFN_vkGetMemoryAndroidHardwareBufferANDROID vkGetMemoryAndroidHardwareBufferANDROID;
	PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID;

#endif // defined(VK_USE_PLATFORM_ANDROID_KHR)

#if defined(VK_EXT_calibrated_timestamps)
	PFN_vkGetCalibratedTimestampsEXT vkGetCalibratedTimestampsEXT;

#endif // defined(VK_EXT_calibrated_timestamps)

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

XRT_CHECK_RESULT const char *
vk_result_string(VkResult code);

XRT_CHECK_RESULT const char *
vk_format_string(VkFormat code);

XRT_CHECK_RESULT const char *
vk_present_mode_string(VkPresentModeKHR code);

XRT_CHECK_RESULT const char *
vk_power_state_string(VkDisplayPowerStateEXT code);

XRT_CHECK_RESULT const char *
vk_color_space_string(VkColorSpaceKHR code);

XRT_CHECK_RESULT const char *
vk_format_feature_string(VkFormatFeatureFlagBits code);

XRT_CHECK_RESULT const char *
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
XRT_CHECK_RESULT bool
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


/*
 *
 * Printing helpers, in the vk_print.c file.
 *
 */

/*!
 * Print device information to the logger at the given logging level,
 * if the vk_bundle has that level enabled.
 *
 * @ingroup aux_vk
 */
void
vk_print_device_info(struct vk_bundle *vk,
                     enum u_logging_level log_level,
                     VkPhysicalDeviceProperties *pdp,
                     uint32_t gpu_index,
                     const char *title);

/*!
 * Print device information about the device that bundle manages at the given
 * logging level if the vk_bundle has that level enabled.
 *
 * @ingroup aux_vk
 */
void
vk_print_opened_device_info(struct vk_bundle *vk, enum u_logging_level log_level);

/*!
 * Print device features to the logger at the given logging level, if the
 * vk_bundle has that level enabled.
 */
void
vk_print_features_info(struct vk_bundle *vk, enum u_logging_level log_level);

/*!
 * Print external handle features to the logger at the given logging level,
 * if the vk_bundle has that level enabled.
 */
void
vk_print_external_handles_info(struct vk_bundle *vk, enum u_logging_level log_level);


/*
 *
 * Struct init functions, in the vk_function_loaders.c file.
 *
 */

/*!
 * Can be done on a completely bare bundle.
 *
 * @ingroup aux_vk
 */
VkResult
vk_get_loader_functions(struct vk_bundle *vk, PFN_vkGetInstanceProcAddr g);

/*!
 * Requires a instance to have been created and set on the bundle.
 *
 * @ingroup aux_vk
 */
VkResult
vk_get_instance_functions(struct vk_bundle *vk);

/*!
 * Requires a device to have been created and set on the bundle.
 *
 * @ingroup aux_vk
 */
VkResult
vk_get_device_functions(struct vk_bundle *vk);


/*
 *
 * Bundle init functions, in the vk_bundle_init.c file.
 *
 */

/*!
 * Only requires @ref vk_get_loader_functions to have been called.
 *
 * @ingroup aux_vk
 */
struct u_string_list *
vk_build_instance_extensions(struct vk_bundle *vk,
                             struct u_string_list *required_instance_ext_list,
                             struct u_string_list *optional_instance_ext_list);

/*!
 * Fills in has_* in vk_bundle given a string of prefiltered instance extensions
 */
void
vk_fill_in_has_instance_extensions(struct vk_bundle *vk, struct u_string_list *ext_list);


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
 * Creates a VkDevice and initialises the VkQueue.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_create_device(struct vk_bundle *vk,
                 int forced_index,
                 bool only_compute,
                 VkQueueGlobalPriorityEXT global_priority,
                 struct u_string_list *required_device_ext_list,
                 struct u_string_list *optional_device_ext_list,
                 const struct vk_device_features *optional_device_features);

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
 *
 * @ingroup aux_vk
 */
VkResult
vk_deinit_mutex(struct vk_bundle *vk);

/*!
 * Requires device and queue to have been set up.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_init_cmd_pool(struct vk_bundle *vk);

/*!
 * Initialize a bundle with objects given to us by client code,
 * used by @ref client_vk_compositor in @ref comp_client.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_init_from_given(struct vk_bundle *vk,
                   PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr,
                   VkInstance instance,
                   VkPhysicalDevice physical_device,
                   VkDevice device,
                   uint32_t queue_family_index,
                   uint32_t queue_index,
                   bool external_fence_fd_enabled,
                   bool external_semaphore_fd_enabled,
                   bool timeline_semaphore_enabled,
                   enum u_logging_level log_level);


/*
 *
 * Other functions.
 *
 */

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
 * @param caller_name Used for error printing, this function is called from
 *        various sources and takes next chains that could influence the result
 *        of various calls inside of it. Since it's up to this function to print
 *        any errors it will add the caller name to error messages.
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
XRT_CHECK_RESULT VkResult
vk_alloc_and_bind_image_memory(struct vk_bundle *vk,
                               VkImage image,
                               size_t max_size,
                               const void *pNext_for_allocate,
                               const char *caller_name,
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
XRT_CHECK_RESULT VkResult
vk_create_image_from_native(struct vk_bundle *vk,
                            const struct xrt_swapchain_create_info *info,
                            struct xrt_image_native *image_native,
                            VkImage *out_image,
                            VkDeviceMemory *out_mem);

/*!
 * Given a DeviceMemory handle created to be exportable, outputs the native buffer type (FD on desktop Linux)
 * equivalent.
 *
 * Caller assumes ownership of handle which should be unreferenced with @ref u_graphics_buffer_unref when no longer
 * needed.
 *
 * @param vk Vulkan bundle
 * @param device_memory The memory to get the handle of
 * @param[out] out_handle A pointer to the handle to populate
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_get_native_handle_from_device_memory(struct vk_bundle *vk,
                                        VkDeviceMemory device_memory,
                                        xrt_graphics_buffer_handle_t *out_handle);

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
 * Helper to create a mutable RG88B8A8 VkImage that specializes in the two
 * UNORM and SRGB variants of that formats.
 *
 * @ingroup aux_vk
 */
VkResult
vk_create_image_mutable_rgba(
    struct vk_bundle *vk, VkExtent2D extent, VkImageUsageFlags usage, VkDeviceMemory *out_mem, VkImage *out_image);

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


/*
 *
 * Helpers for creating ímage views.
 *
 */

/*!
 * @ingroup aux_vk
 */
VkResult
vk_create_view(struct vk_bundle *vk,
               VkImage image,
               VkImageViewType type,
               VkFormat format,
               VkImageSubresourceRange subresource_range,
               VkImageView *out_view);

/*!
 * @ingroup aux_vk
 */
VkResult
vk_create_view_swizzle(struct vk_bundle *vk,
                       VkImage image,
                       VkImageViewType type,
                       VkFormat format,
                       VkImageSubresourceRange subresource_range,
                       VkComponentMapping components,
                       VkImageView *out_view);

/*!
 * Creates a image with a specific subset of usage, useful for a mutable images
 * where one format might not support all usages defined by the image.
 *
 * @ingroup aux_vk
 */
VkResult
vk_create_view_usage(struct vk_bundle *vk,
                     VkImage image,
                     VkImageViewType type,
                     VkFormat format,
                     VkImageUsageFlags image_usage,
                     VkImageSubresourceRange subresource_range,
                     VkImageView *out_view);


/*
 *
 * Helpers for creating descriptor pools and sets.
 *
 */

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


/*
 *
 * Helpers for creating buffers.
 *
 */

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


/*
 *
 * Helpers for writing command buffers using the global command pool.
 *
 */

/*!
 * Create a new command buffer, takes the pool lock.
 *
 * @pre Requires successful call to vk_init_mutex.
 *
 * @ingroup aux_vk
 */
VkResult
vk_cmd_buffer_create(struct vk_bundle *vk, VkCommandBuffer *out_cmd_buffer);

/*!
 * Create and begins a new command buffer, takes the pool lock.
 *
 * @pre Requires successful call to vk_init_mutex.
 *
 * @ingroup aux_vk
 */
VkResult
vk_cmd_buffer_create_and_begin(struct vk_bundle *vk, VkCommandBuffer *out_cmd_buffer);

/*!
 * A do everything command buffer submission function, during the operation
 * the pool lock will be taken and released.
 *
 * * Creates a new fence.
 * * Submits @p cmd_buffer to the queue, along with the fence.
 * * Waits for the fence to complete.
 * * Destroys the fence.
 * * Destroy @p cmd_buffer.
 *
 * @pre Requires successful call to vk_init_mutex.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_cmd_buffer_submit(struct vk_bundle *vk, VkCommandBuffer cmd_buffer);

/*!
 * Submits to the given queue, with the given fence.
 *
 * @pre Requires successful call to vk_init_mutex.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_locked_submit(struct vk_bundle *vk, VkQueue queue, uint32_t count, const VkSubmitInfo *infos, VkFence fence);

/*!
 * Set the image layout using a barrier command, takes the pool lock.
 *
 * @pre Requires successful call to vk_init_mutex.
 *
 * @ingroup aux_vk
 */
void
vk_cmd_image_barrier_gpu(struct vk_bundle *vk,
                         VkCommandBuffer cmd_buffer,
                         VkImage image,
                         VkAccessFlags src_access_mask,
                         VkAccessFlags dst_access_mask,
                         VkImageLayout old_layout,
                         VkImageLayout new_layout,
                         VkImageSubresourceRange subresource_range);

/*!
 * Inserts a image barrier command, doesn't take any locks.
 *
 * @ingroup aux_vk
 */
void
vk_cmd_image_barrier_locked(struct vk_bundle *vk,
                            VkCommandBuffer cmd_buffer,
                            VkImage image,
                            VkAccessFlags src_access_mask,
                            VkAccessFlags dst_access_mask,
                            VkImageLayout old_image_layout,
                            VkImageLayout new_image_layout,
                            VkPipelineStageFlags src_stage_mask,
                            VkPipelineStageFlags dst_stage_mask,
                            VkImageSubresourceRange subresource_range);

/*!
 * Inserts a image barrier command specifically for GPU commands,
 * doesn't take any locks.
 *
 * @ingroup aux_vk
 */
void
vk_cmd_image_barrier_gpu_locked(struct vk_bundle *vk,
                                VkCommandBuffer cmd_buffer,
                                VkImage image,
                                VkAccessFlags src_access_mask,
                                VkAccessFlags dst_access_mask,
                                VkImageLayout old_layout,
                                VkImageLayout new_layout,
                                VkImageSubresourceRange subresource_range);


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
                           const VkSpecializationInfo *specialization_info,
                           VkPipeline *out_compute_pipeline);


/*
 *
 * Compositor buffer and swapchain image flags helpers, in the vk_compositor_flags.c file.
 *
 */

/*!
 * Return the extern handle type that a buffer should be created with.
 *
 * cb = Compositor Buffer.
 */
VkExternalMemoryHandleTypeFlags
vk_cb_get_buffer_external_handle_type(struct vk_bundle *vk);

/*!
 * Helper for all of the supported formats to check support for.
 *
 * These are the available formats we will expose to our clients.
 *
 * In order of what we prefer. Start with a SRGB format that works on
 * both OpenGL and Vulkan. The two linear formats that works on both
 * OpenGL and Vulkan. A SRGB format that only works on Vulkan. The last
 * two formats should not be used as they are linear but doesn't have
 * enough bits to express it without resulting in banding.
 *
 * The format VK_FORMAT_A2B10G10R10_UNORM_PACK32 is not listed since
 * 10 bits are not considered enough to do linear colors without
 * banding. If there was a sRGB variant of it then we would have used it
 * instead but there isn't. Since it's not a popular format it's best
 * not to list it rather then listing it and people falling into the
 * trap. The absolute minimum is R11G11B10, but is a really weird format
 * so we are not exposing it.
 *
 * CSCI = Compositor SwapChain Images.
 *
 * @ingroup aux_vk
 */
#define VK_CSCI_FORMATS(THING_COLOR, THING_DS, THING_D, THING_S)                                                       \
	/* color formats */                                                                                            \
	THING_COLOR(R16G16B16A16_UNORM)  /* OGL VK */                                                                  \
	THING_COLOR(R16G16B16A16_SFLOAT) /* OGL VK */                                                                  \
	THING_COLOR(R16G16B16_UNORM)     /* OGL VK - Uncommon. */                                                      \
	THING_COLOR(R16G16B16_SFLOAT)    /* OGL VK - Uncommon. */                                                      \
	THING_COLOR(R8G8B8A8_SRGB)       /* OGL VK */                                                                  \
	THING_COLOR(B8G8R8A8_SRGB)       /* VK */                                                                      \
	THING_COLOR(R8G8B8_SRGB)         /* OGL VK - Uncommon. */                                                      \
	THING_COLOR(R8G8B8A8_UNORM)      /* OGL VK - Bad color precision. */                                           \
	THING_COLOR(B8G8R8A8_UNORM)      /* VK     - Bad color precision. */                                           \
	THING_COLOR(R8G8B8_UNORM)        /* OGL VK - Uncommon. Bad color precision. */                                 \
	THING_COLOR(B8G8R8_UNORM)        /* VK     - Uncommon. Bad color precision. */                                 \
	THING_COLOR(R5G6B5_UNORM_PACK16) /* OLG VK - Bad color precision. */                                           \
	/* depth formats */                                                                                            \
	THING_D(D32_SFLOAT)          /* OGL VK */                                                                      \
	THING_D(D16_UNORM)           /* OGL VK */                                                                      \
	THING_D(X8_D24_UNORM_PACK32) /* OGL VK */                                                                      \
	/* depth stencil formats */                                                                                    \
	THING_DS(D24_UNORM_S8_UINT)  /* OGL VK */                                                                      \
	THING_DS(D32_SFLOAT_S8_UINT) /* OGL VK */                                                                      \
	/* stencil format */                                                                                           \
	THING_S(S8_UINT)

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
 * Return the extern handle type that a image should be created with.
 *
 * CSCI = Compositor SwapChain Images.
 */
VkExternalMemoryHandleTypeFlags
vk_csci_get_image_external_handle_type(struct vk_bundle *vk);

/*!
 * Get whether a given image can be imported/exported for a handle type.
 *
 * CSCI = Compositor SwapChain Images.
 */
void
vk_csci_get_image_external_support(struct vk_bundle *vk,
                                   VkFormat image_format,
                                   enum xrt_swapchain_usage_bits bits,
                                   VkExternalMemoryHandleTypeFlags handle_type,
                                   bool *out_importable,
                                   bool *out_exportable);


/*
 *
 * Sync objects, in the vk_sync_objects.c file.
 *
 */

/*!
 * Is there a good likelihood that the import/export of a timeline semaphore
 * will succeed, in other words will the below functions work.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT bool
vk_can_import_and_export_timeline_semaphore(struct vk_bundle *vk);

/*!
 * @brief Creates a Vulkan fence, submits it to the default VkQueue and return
 * its native graphics sync handle.
 *
 * In case of error, out_native is not touched by the function.
 *
 * See @ref vk_create_fence_sync_from_native for ownership semantics on import.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_create_and_submit_fence_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t *out_native);

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
XRT_CHECK_RESULT VkResult
vk_create_fence_sync_from_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t native, VkFence *out_fence);

/*!
 * Creates a Vulkan semaphore and a native graphics sync handle.
 *
 * In case of success, the underlying Vulkan functionality's ownership semantics
 * apply: ownership of the @p native handle may have transferred, a reference
 * may have been added, or the Vulkan object may rely on the caller to keep the
 * native handle alive until the Vulkan object is destroyed. Which option
 * applies depends on the particular native handle type used.
 *
 * In case of error, neither @p out_sem and @p out_native is not touched by the
 * function so the caller only becomes responsible for the output on success.
 *
 * See the corresponding Vulkan specification text:
 * https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/vkspec.html#synchronization-semaphores
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_create_semaphore_and_native(struct vk_bundle *vk, VkSemaphore *out_sem, xrt_graphics_sync_handle_t *out_native);

#if defined(VK_KHR_timeline_semaphore) || defined(XRT_DOXYGEN)
/*
 * Creates a Vulkan timeline semaphore and a native graphics sync
 * handle, see @ref vk_create_semaphore_and_native for more details.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_create_timeline_semaphore_and_native(struct vk_bundle *vk,
                                        VkSemaphore *out_sem,
                                        xrt_graphics_sync_handle_t *out_native);
#endif

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
XRT_CHECK_RESULT VkResult
vk_create_semaphore_from_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t native, VkSemaphore *out_sem);

#if defined(VK_KHR_timeline_semaphore) || defined(XRT_DOXYGEN)
/*!
 * @brief Creates a Vulkan timeline semaphore from a native graphics sync
 * handle, see @ref vk_create_semaphore_from_native for more details.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_create_timeline_semaphore_from_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t native, VkSemaphore *out_sem);
#endif


/*
 *
 * Time function(s), in the vk_time.c file.
 *
 */

#if defined(VK_EXT_calibrated_timestamps) || defined(XRT_DOXYGEN)
/*!
 * Convert timestamps in GPU ticks (as return by VkQueryPool timestamp queries)
 * into host CPU nanoseconds, same time domain as @ref os_monotonic_get_ns.
 *
 * Note the timestamp needs to be in the past and not to old, this is because
 * not all GPU has full 64 bit timer resolution. For instance a Intel GPU "only"
 * have 36 bits of valid timestamp and a tick period 83.3333 nanosecond,
 * equating to an epoch of 5726 seconds before overflowing. The function can
 * handle overflows happening between the given timestamps and when it is called
 * but only for one such epoch overflow, any more will only be treated as one
 * such overflow. So timestamps needs to be converted resonably soon after they
 * have been captured.
 *
 * @param vk                The Vulkan bundle.
 * @param count             Number of timestamps to be converted.
 * @param[in,out] in_out_timestamps Array of timestamps to be converted, done in place.
 *
 * @ingroup aux_vk
 */
XRT_CHECK_RESULT VkResult
vk_convert_timestamps_to_host_ns(struct vk_bundle *vk, uint32_t count, uint64_t *in_out_timestamps);
#endif


#ifdef __cplusplus
}
#endif
