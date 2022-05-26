// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Functions to fill in the functions.
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

#include "vk/vk_helpers.h"


/*
 *
 * Helpers.
 *
 */

#define GET_PROC(vk, name) (PFN_##name) vk->vkGetInstanceProcAddr(NULL, #name);

#define GET_INS_PROC(vk, name) (PFN_##name) vk->vkGetInstanceProcAddr(vk->instance, #name);

#define GET_DEV_PROC(vk, name) (PFN_##name) vk->vkGetDeviceProcAddr(vk->device, #name);


/*
 *
 * 'Exported' functions.
 *
 */

VkResult
vk_get_loader_functions(struct vk_bundle *vk, PFN_vkGetInstanceProcAddr g)
{
	vk->vkGetInstanceProcAddr = g;

	// Fill in all loader functions.
	// clang-format off
	vk->vkCreateInstance = GET_PROC(vk, vkCreateInstance);
	vk->vkEnumerateInstanceExtensionProperties = GET_PROC(vk, vkEnumerateInstanceExtensionProperties);
	// clang-format on

	return VK_SUCCESS;
}

VkResult
vk_get_instance_functions(struct vk_bundle *vk)
{
	// clang-format off
	// beginning of GENERATED instance loader code - do not modify - used by scripts
	vk->vkDestroyInstance                                 = GET_INS_PROC(vk, vkDestroyInstance);
	vk->vkGetDeviceProcAddr                               = GET_INS_PROC(vk, vkGetDeviceProcAddr);
	vk->vkCreateDevice                                    = GET_INS_PROC(vk, vkCreateDevice);
	vk->vkDestroySurfaceKHR                               = GET_INS_PROC(vk, vkDestroySurfaceKHR);

	vk->vkCreateDebugReportCallbackEXT                    = GET_INS_PROC(vk, vkCreateDebugReportCallbackEXT);
	vk->vkDestroyDebugReportCallbackEXT                   = GET_INS_PROC(vk, vkDestroyDebugReportCallbackEXT);

	vk->vkEnumeratePhysicalDevices                        = GET_INS_PROC(vk, vkEnumeratePhysicalDevices);
	vk->vkGetPhysicalDeviceProperties                     = GET_INS_PROC(vk, vkGetPhysicalDeviceProperties);
	vk->vkGetPhysicalDeviceProperties2                    = GET_INS_PROC(vk, vkGetPhysicalDeviceProperties2);
	vk->vkGetPhysicalDeviceFeatures2                      = GET_INS_PROC(vk, vkGetPhysicalDeviceFeatures2);
	vk->vkGetPhysicalDeviceMemoryProperties               = GET_INS_PROC(vk, vkGetPhysicalDeviceMemoryProperties);
	vk->vkGetPhysicalDeviceQueueFamilyProperties          = GET_INS_PROC(vk, vkGetPhysicalDeviceQueueFamilyProperties);
	vk->vkGetPhysicalDeviceSurfaceCapabilitiesKHR         = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
	vk->vkGetPhysicalDeviceSurfaceFormatsKHR              = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfaceFormatsKHR);
	vk->vkGetPhysicalDeviceSurfacePresentModesKHR         = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfacePresentModesKHR);
	vk->vkGetPhysicalDeviceSurfaceSupportKHR              = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfaceSupportKHR);
	vk->vkGetPhysicalDeviceFormatProperties               = GET_INS_PROC(vk, vkGetPhysicalDeviceFormatProperties);
	vk->vkGetPhysicalDeviceImageFormatProperties2         = GET_INS_PROC(vk, vkGetPhysicalDeviceImageFormatProperties2);
	vk->vkGetPhysicalDeviceExternalBufferPropertiesKHR    = GET_INS_PROC(vk, vkGetPhysicalDeviceExternalBufferPropertiesKHR);
	vk->vkGetPhysicalDeviceExternalFencePropertiesKHR     = GET_INS_PROC(vk, vkGetPhysicalDeviceExternalFencePropertiesKHR);
	vk->vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = GET_INS_PROC(vk, vkGetPhysicalDeviceExternalSemaphorePropertiesKHR);
	vk->vkEnumerateDeviceExtensionProperties              = GET_INS_PROC(vk, vkEnumerateDeviceExtensionProperties);
	vk->vkEnumerateDeviceLayerProperties                  = GET_INS_PROC(vk, vkEnumerateDeviceLayerProperties);

#if defined(VK_EXT_calibrated_timestamps)
	vk->vkGetPhysicalDeviceCalibrateableTimeDomainsEXT    = GET_INS_PROC(vk, vkGetPhysicalDeviceCalibrateableTimeDomainsEXT);

#endif // defined(VK_EXT_calibrated_timestamps)

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
	vk->vkCreateDisplayPlaneSurfaceKHR                    = GET_INS_PROC(vk, vkCreateDisplayPlaneSurfaceKHR);
	vk->vkGetDisplayPlaneCapabilitiesKHR                  = GET_INS_PROC(vk, vkGetDisplayPlaneCapabilitiesKHR);
	vk->vkGetPhysicalDeviceDisplayPropertiesKHR           = GET_INS_PROC(vk, vkGetPhysicalDeviceDisplayPropertiesKHR);
	vk->vkGetPhysicalDeviceDisplayPlanePropertiesKHR      = GET_INS_PROC(vk, vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
	vk->vkGetDisplayModePropertiesKHR                     = GET_INS_PROC(vk, vkGetDisplayModePropertiesKHR);
	vk->vkReleaseDisplayEXT                               = GET_INS_PROC(vk, vkReleaseDisplayEXT);

#endif // defined(VK_USE_PLATFORM_DISPLAY_KHR)

#if defined(VK_USE_PLATFORM_XCB_KHR)
	vk->vkCreateXcbSurfaceKHR                             = GET_INS_PROC(vk, vkCreateXcbSurfaceKHR);

#endif // defined(VK_USE_PLATFORM_XCB_KHR)

#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	vk->vkCreateWaylandSurfaceKHR                         = GET_INS_PROC(vk, vkCreateWaylandSurfaceKHR);

#endif // defined(VK_USE_PLATFORM_WAYLAND_KHR)

#if defined(VK_USE_PLATFORM_WAYLAND_KHR) && defined(VK_EXT_acquire_drm_display)
	vk->vkAcquireDrmDisplayEXT                            = GET_INS_PROC(vk, vkAcquireDrmDisplayEXT);
	vk->vkGetDrmDisplayEXT                                = GET_INS_PROC(vk, vkGetDrmDisplayEXT);

#endif // defined(VK_USE_PLATFORM_WAYLAND_KHR) && defined(VK_EXT_acquire_drm_display)

#if defined(VK_USE_PLATFORM_XLIB_XRANDR_EXT)
	vk->vkGetRandROutputDisplayEXT                        = GET_INS_PROC(vk, vkGetRandROutputDisplayEXT);
	vk->vkAcquireXlibDisplayEXT                           = GET_INS_PROC(vk, vkAcquireXlibDisplayEXT);

#endif // defined(VK_USE_PLATFORM_XLIB_XRANDR_EXT)

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	vk->vkCreateAndroidSurfaceKHR                         = GET_INS_PROC(vk, vkCreateAndroidSurfaceKHR);

#endif // defined(VK_USE_PLATFORM_ANDROID_KHR)

#if defined(VK_USE_PLATFORM_WIN32_KHR)
	vk->vkCreateWin32SurfaceKHR                           = GET_INS_PROC(vk, vkCreateWin32SurfaceKHR);

#endif // defined(VK_USE_PLATFORM_WIN32_KHR)

#if defined(VK_EXT_display_surface_counter)
	vk->vkGetPhysicalDeviceSurfaceCapabilities2EXT        = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfaceCapabilities2EXT);
#endif // defined(VK_EXT_display_surface_counter)

	// end of GENERATED instance loader code - do not modify - used by scripts

	// clang-format on
	return VK_SUCCESS;
}

VkResult
vk_get_device_functions(struct vk_bundle *vk)
{

	// clang-format off
	// beginning of GENERATED device loader code - do not modify - used by scripts
	vk->vkDestroyDevice                             = GET_DEV_PROC(vk, vkDestroyDevice);
	vk->vkDeviceWaitIdle                            = GET_DEV_PROC(vk, vkDeviceWaitIdle);
	vk->vkAllocateMemory                            = GET_DEV_PROC(vk, vkAllocateMemory);
	vk->vkFreeMemory                                = GET_DEV_PROC(vk, vkFreeMemory);
	vk->vkMapMemory                                 = GET_DEV_PROC(vk, vkMapMemory);
	vk->vkUnmapMemory                               = GET_DEV_PROC(vk, vkUnmapMemory);

	vk->vkCreateBuffer                              = GET_DEV_PROC(vk, vkCreateBuffer);
	vk->vkDestroyBuffer                             = GET_DEV_PROC(vk, vkDestroyBuffer);
	vk->vkBindBufferMemory                          = GET_DEV_PROC(vk, vkBindBufferMemory);

	vk->vkCreateImage                               = GET_DEV_PROC(vk, vkCreateImage);
	vk->vkDestroyImage                              = GET_DEV_PROC(vk, vkDestroyImage);
	vk->vkBindImageMemory                           = GET_DEV_PROC(vk, vkBindImageMemory);

	vk->vkGetBufferMemoryRequirements               = GET_DEV_PROC(vk, vkGetBufferMemoryRequirements);
	vk->vkFlushMappedMemoryRanges                   = GET_DEV_PROC(vk, vkFlushMappedMemoryRanges);
	vk->vkGetImageMemoryRequirements                = GET_DEV_PROC(vk, vkGetImageMemoryRequirements);
	vk->vkGetImageMemoryRequirements2               = GET_DEV_PROC(vk, vkGetImageMemoryRequirements2KHR);
	vk->vkGetImageSubresourceLayout                 = GET_DEV_PROC(vk, vkGetImageSubresourceLayout);

	vk->vkCreateImageView                           = GET_DEV_PROC(vk, vkCreateImageView);
	vk->vkDestroyImageView                          = GET_DEV_PROC(vk, vkDestroyImageView);

	vk->vkCreateSampler                             = GET_DEV_PROC(vk, vkCreateSampler);
	vk->vkDestroySampler                            = GET_DEV_PROC(vk, vkDestroySampler);

	vk->vkCreateShaderModule                        = GET_DEV_PROC(vk, vkCreateShaderModule);
	vk->vkDestroyShaderModule                       = GET_DEV_PROC(vk, vkDestroyShaderModule);

	vk->vkCreateQueryPool                           = GET_DEV_PROC(vk, vkCreateQueryPool);
	vk->vkDestroyQueryPool                          = GET_DEV_PROC(vk, vkDestroyQueryPool);
	vk->vkGetQueryPoolResults                       = GET_DEV_PROC(vk, vkGetQueryPoolResults);

	vk->vkCreateCommandPool                         = GET_DEV_PROC(vk, vkCreateCommandPool);
	vk->vkDestroyCommandPool                        = GET_DEV_PROC(vk, vkDestroyCommandPool);
	vk->vkResetCommandPool                          = GET_DEV_PROC(vk, vkResetCommandPool);

	vk->vkAllocateCommandBuffers                    = GET_DEV_PROC(vk, vkAllocateCommandBuffers);
	vk->vkBeginCommandBuffer                        = GET_DEV_PROC(vk, vkBeginCommandBuffer);
	vk->vkCmdBeginQuery                             = GET_DEV_PROC(vk, vkCmdBeginQuery);
	vk->vkCmdCopyQueryPoolResults                   = GET_DEV_PROC(vk, vkCmdCopyQueryPoolResults);
	vk->vkCmdEndQuery                               = GET_DEV_PROC(vk, vkCmdEndQuery);
	vk->vkCmdResetQueryPool                         = GET_DEV_PROC(vk, vkCmdResetQueryPool);
	vk->vkCmdWriteTimestamp                         = GET_DEV_PROC(vk, vkCmdWriteTimestamp);
	vk->vkCmdPipelineBarrier                        = GET_DEV_PROC(vk, vkCmdPipelineBarrier);
	vk->vkCmdBeginRenderPass                        = GET_DEV_PROC(vk, vkCmdBeginRenderPass);
	vk->vkCmdSetScissor                             = GET_DEV_PROC(vk, vkCmdSetScissor);
	vk->vkCmdSetViewport                            = GET_DEV_PROC(vk, vkCmdSetViewport);
	vk->vkCmdClearColorImage                        = GET_DEV_PROC(vk, vkCmdClearColorImage);
	vk->vkCmdEndRenderPass                          = GET_DEV_PROC(vk, vkCmdEndRenderPass);
	vk->vkCmdBindDescriptorSets                     = GET_DEV_PROC(vk, vkCmdBindDescriptorSets);
	vk->vkCmdBindPipeline                           = GET_DEV_PROC(vk, vkCmdBindPipeline);
	vk->vkCmdBindVertexBuffers                      = GET_DEV_PROC(vk, vkCmdBindVertexBuffers);
	vk->vkCmdBindIndexBuffer                        = GET_DEV_PROC(vk, vkCmdBindIndexBuffer);
	vk->vkCmdDraw                                   = GET_DEV_PROC(vk, vkCmdDraw);
	vk->vkCmdDrawIndexed                            = GET_DEV_PROC(vk, vkCmdDrawIndexed);
	vk->vkCmdDispatch                               = GET_DEV_PROC(vk, vkCmdDispatch);
	vk->vkCmdCopyBuffer                             = GET_DEV_PROC(vk, vkCmdCopyBuffer);
	vk->vkCmdCopyBufferToImage                      = GET_DEV_PROC(vk, vkCmdCopyBufferToImage);
	vk->vkCmdCopyImage                              = GET_DEV_PROC(vk, vkCmdCopyImage);
	vk->vkCmdCopyImageToBuffer                      = GET_DEV_PROC(vk, vkCmdCopyImageToBuffer);
	vk->vkCmdBlitImage                              = GET_DEV_PROC(vk, vkCmdBlitImage);
	vk->vkEndCommandBuffer                          = GET_DEV_PROC(vk, vkEndCommandBuffer);
	vk->vkFreeCommandBuffers                        = GET_DEV_PROC(vk, vkFreeCommandBuffers);

	vk->vkCreateRenderPass                          = GET_DEV_PROC(vk, vkCreateRenderPass);
	vk->vkDestroyRenderPass                         = GET_DEV_PROC(vk, vkDestroyRenderPass);

	vk->vkCreateFramebuffer                         = GET_DEV_PROC(vk, vkCreateFramebuffer);
	vk->vkDestroyFramebuffer                        = GET_DEV_PROC(vk, vkDestroyFramebuffer);

	vk->vkCreatePipelineCache                       = GET_DEV_PROC(vk, vkCreatePipelineCache);
	vk->vkDestroyPipelineCache                      = GET_DEV_PROC(vk, vkDestroyPipelineCache);

	vk->vkResetDescriptorPool                       = GET_DEV_PROC(vk, vkResetDescriptorPool);
	vk->vkCreateDescriptorPool                      = GET_DEV_PROC(vk, vkCreateDescriptorPool);
	vk->vkDestroyDescriptorPool                     = GET_DEV_PROC(vk, vkDestroyDescriptorPool);

	vk->vkAllocateDescriptorSets                    = GET_DEV_PROC(vk, vkAllocateDescriptorSets);
	vk->vkFreeDescriptorSets                        = GET_DEV_PROC(vk, vkFreeDescriptorSets);

	vk->vkCreateComputePipelines                    = GET_DEV_PROC(vk, vkCreateComputePipelines);
	vk->vkCreateGraphicsPipelines                   = GET_DEV_PROC(vk, vkCreateGraphicsPipelines);
	vk->vkDestroyPipeline                           = GET_DEV_PROC(vk, vkDestroyPipeline);

	vk->vkCreatePipelineLayout                      = GET_DEV_PROC(vk, vkCreatePipelineLayout);
	vk->vkDestroyPipelineLayout                     = GET_DEV_PROC(vk, vkDestroyPipelineLayout);

	vk->vkCreateDescriptorSetLayout                 = GET_DEV_PROC(vk, vkCreateDescriptorSetLayout);
	vk->vkUpdateDescriptorSets                      = GET_DEV_PROC(vk, vkUpdateDescriptorSets);
	vk->vkDestroyDescriptorSetLayout                = GET_DEV_PROC(vk, vkDestroyDescriptorSetLayout);

	vk->vkGetDeviceQueue                            = GET_DEV_PROC(vk, vkGetDeviceQueue);
	vk->vkQueueSubmit                               = GET_DEV_PROC(vk, vkQueueSubmit);
	vk->vkQueueWaitIdle                             = GET_DEV_PROC(vk, vkQueueWaitIdle);

	vk->vkCreateSemaphore                           = GET_DEV_PROC(vk, vkCreateSemaphore);
#if defined(VK_KHR_timeline_semaphore)
	vk->vkSignalSemaphore                           = GET_DEV_PROC(vk, vkSignalSemaphoreKHR);
	vk->vkWaitSemaphores                            = GET_DEV_PROC(vk, vkWaitSemaphoresKHR);
	vk->vkGetSemaphoreCounterValue                  = GET_DEV_PROC(vk, vkGetSemaphoreCounterValueKHR);
#endif // defined(VK_KHR_timeline_semaphore)

	vk->vkDestroySemaphore                          = GET_DEV_PROC(vk, vkDestroySemaphore);

	vk->vkCreateFence                               = GET_DEV_PROC(vk, vkCreateFence);
	vk->vkWaitForFences                             = GET_DEV_PROC(vk, vkWaitForFences);
	vk->vkGetFenceStatus                            = GET_DEV_PROC(vk, vkGetFenceStatus);
	vk->vkDestroyFence                              = GET_DEV_PROC(vk, vkDestroyFence);
	vk->vkResetFences                               = GET_DEV_PROC(vk, vkResetFences);

	vk->vkCreateSwapchainKHR                        = GET_DEV_PROC(vk, vkCreateSwapchainKHR);
	vk->vkDestroySwapchainKHR                       = GET_DEV_PROC(vk, vkDestroySwapchainKHR);
	vk->vkGetSwapchainImagesKHR                     = GET_DEV_PROC(vk, vkGetSwapchainImagesKHR);
	vk->vkAcquireNextImageKHR                       = GET_DEV_PROC(vk, vkAcquireNextImageKHR);
	vk->vkQueuePresentKHR                           = GET_DEV_PROC(vk, vkQueuePresentKHR);

#if defined(VK_USE_PLATFORM_WIN32_KHR)
	vk->vkGetMemoryWin32HandleKHR                   = GET_DEV_PROC(vk, vkGetMemoryWin32HandleKHR);
	vk->vkGetFenceWin32HandleKHR                    = GET_DEV_PROC(vk, vkGetFenceWin32HandleKHR);
	vk->vkGetSemaphoreWin32HandleKHR                = GET_DEV_PROC(vk, vkGetSemaphoreWin32HandleKHR);
	vk->vkImportFenceWin32HandleKHR                 = GET_DEV_PROC(vk, vkImportFenceWin32HandleKHR);
	vk->vkImportSemaphoreWin32HandleKHR             = GET_DEV_PROC(vk, vkImportSemaphoreWin32HandleKHR);

#endif // defined(VK_USE_PLATFORM_WIN32_KHR)

#if !defined(VK_USE_PLATFORM_WIN32_KHR)
	vk->vkGetMemoryFdKHR                            = GET_DEV_PROC(vk, vkGetMemoryFdKHR);
	vk->vkGetFenceFdKHR                             = GET_DEV_PROC(vk, vkGetFenceFdKHR);
	vk->vkGetSemaphoreFdKHR                         = GET_DEV_PROC(vk, vkGetSemaphoreFdKHR);
	vk->vkImportFenceFdKHR                          = GET_DEV_PROC(vk, vkImportFenceFdKHR);
	vk->vkImportSemaphoreFdKHR                      = GET_DEV_PROC(vk, vkImportSemaphoreFdKHR);

#endif // !defined(VK_USE_PLATFORM_WIN32_KHR)

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	vk->vkGetMemoryAndroidHardwareBufferANDROID     = GET_DEV_PROC(vk, vkGetMemoryAndroidHardwareBufferANDROID);
	vk->vkGetAndroidHardwareBufferPropertiesANDROID = GET_DEV_PROC(vk, vkGetAndroidHardwareBufferPropertiesANDROID);

#endif // defined(VK_USE_PLATFORM_ANDROID_KHR)

#if defined(VK_EXT_calibrated_timestamps)
	vk->vkGetCalibratedTimestampsEXT                = GET_DEV_PROC(vk, vkGetCalibratedTimestampsEXT);

#endif // defined(VK_EXT_calibrated_timestamps)

	vk->vkGetPastPresentationTimingGOOGLE           = GET_DEV_PROC(vk, vkGetPastPresentationTimingGOOGLE);

#if defined(VK_EXT_display_control)
	vk->vkGetSwapchainCounterEXT                    = GET_DEV_PROC(vk, vkGetSwapchainCounterEXT);
	vk->vkRegisterDeviceEventEXT                    = GET_DEV_PROC(vk, vkRegisterDeviceEventEXT);
	vk->vkRegisterDisplayEventEXT                   = GET_DEV_PROC(vk, vkRegisterDisplayEventEXT);
#endif // defined(VK_EXT_display_control)

	// end of GENERATED device loader code - do not modify - used by scripts
	// clang-format on
	return VK_SUCCESS;
}
