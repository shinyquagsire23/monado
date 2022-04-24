// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Prining helper code.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup aux_vk
 */

#include "vk/vk_helpers.h"


void
vk_print_device_info(struct vk_bundle *vk,
                     enum u_logging_level log_level,
                     VkPhysicalDeviceProperties *pdp,
                     uint32_t gpu_index,
                     const char *title)
{
	U_LOG_IFL(log_level, vk->log_level,
	          "%s"
	          "\tname: %s\n"
	          "\tvendor: 0x%04x\n"
	          "\tproduct: 0x%04x\n"
	          "\tapiVersion: %u.%u.%u\n"
	          "\tdriverVersion: %u.%u.%u",
	          title, pdp->deviceName, pdp->vendorID, pdp->deviceID, VK_VERSION_MAJOR(pdp->apiVersion),
	          VK_VERSION_MINOR(pdp->apiVersion), VK_VERSION_PATCH(pdp->apiVersion),
	          VK_VERSION_MAJOR(pdp->driverVersion), VK_VERSION_MINOR(pdp->driverVersion),
	          VK_VERSION_PATCH(pdp->driverVersion));
}

void
vk_print_features_info(struct vk_bundle *vk, enum u_logging_level log_level)
{
	U_LOG_IFL(log_level, vk->log_level,                                       //
	          "Features:"                                                     //
	          "\n\ttimestamp_compute_and_graphics: %s"                        //
	          "\n\ttimestamp_period: %f"                                      //
	          "\n\ttimestamp_valid_bits: %u"                                  //
	          "\n\ttimeline_semaphore: %s",                                   //
	          vk->features.timestamp_compute_and_graphics ? "true" : "false", //
	          vk->features.timestamp_period,                                  //
	          vk->features.timestamp_valid_bits,                              //
	          vk->features.timeline_semaphore ? "true" : "false");            //
}

void
vk_print_external_handles_info(struct vk_bundle *vk, enum u_logging_level log_level)
{
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)

	U_LOG_IFL(log_level, vk->log_level,                         //
	          "Supported fences:\n\t%s: %s\n\t%s: %s",          //
	          "VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT",      //
	          vk->external.fence_sync_fd ? "true" : "false",    //
	          "VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT",    //
	          vk->external.fence_opaque_fd ? "true" : "false"); //

	U_LOG_IFL(log_level, vk->log_level,                                        //
	          "Supported semaphores:\n\t%s: %s\n\t%s: %s\n\t%s: %s\n\t%s: %s", //
	          "VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT(binary)",         //
	          vk->external.binary_semaphore_sync_fd ? "true" : "false",        //
	          "VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT(binary)",       //
	          vk->external.binary_semaphore_opaque_fd ? "true" : "false",      //
	          "VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT(timeline)",       //
	          vk->external.timeline_semaphore_sync_fd ? "true" : "false",      //
	          "VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT(timeline)",     //
	          vk->external.timeline_semaphore_opaque_fd ? "true" : "false");   //

#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)

	U_LOG_IFL(log_level, vk->log_level,                            //
	          "Supported fences:\n\t%s: %s",                       //
	          "VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_BIT",    //
	          vk->external.fence_win32_handle ? "true" : "false"); //

	U_LOG_IFL(log_level, vk->log_level,                                         //
	          "Supported semaphores:\n\t%s: %s\n\t%s: %s",                      //
	          "VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT(binary)",     //
	          vk->external.binary_semaphore_win32_handle ? "true" : "false",    //
	          "VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT(timeline)",   //
	          vk->external.timeline_semaphore_win32_handle ? "true" : "false"); //

#else
#error "Need port for fence sync handles printers"
#endif
}
