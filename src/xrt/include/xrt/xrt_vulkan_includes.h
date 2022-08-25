// Copyright 2018-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Include all of the Vulkan headers in one place, and cope with any "messy" includes implied by it.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_config_vulkan.h"
#include "xrt/xrt_config_have.h"

#ifdef XRT_HAVE_VULKAN
// pre-emptively include windows.h if applicable so we can specify our own flags for it.
#include "xrt/xrt_windows.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

// Dealing with underscore compat.
#ifndef VK_KHR_MAINTENANCE_1_EXTENSION_NAME
#define VK_KHR_MAINTENANCE_1_EXTENSION_NAME VK_KHR_MAINTENANCE1_EXTENSION_NAME
#endif
#ifndef VK_KHR_MAINTENANCE_2_EXTENSION_NAME
#define VK_KHR_MAINTENANCE_2_EXTENSION_NAME VK_KHR_MAINTENANCE2_EXTENSION_NAME
#endif
#ifndef VK_KHR_MAINTENANCE_3_EXTENSION_NAME
#define VK_KHR_MAINTENANCE_3_EXTENSION_NAME VK_KHR_MAINTENANCE3_EXTENSION_NAME
#endif

#if defined(VK_USE_PLATFORM_XLIB_XRANDR_EXT) || defined(VK_USE_PLATFORM_XLIB_KHR)
// the xlib header is notoriously polluting.
#undef Status
#undef Bool
#endif

#ifdef __cplusplus
extern "C" {
#endif

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName);

#ifdef __cplusplus
}
#endif

#endif // XRT_HAVE_VULKAN
