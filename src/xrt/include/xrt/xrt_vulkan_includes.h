// Copyright 2018-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Include all of the Vulkan headers in one place.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_config_vulkan.h"
#include "xrt/xrt_config_have.h"

#ifdef XRT_HAVE_VULKAN
#include "xrt/xrt_windows.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName);

#ifdef __cplusplus
}
#endif

#endif // XRT_HAVE_VULKAN
