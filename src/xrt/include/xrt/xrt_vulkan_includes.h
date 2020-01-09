// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Include all of the Vulkan headers in one place.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C"
#endif
    VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
    vkGetInstanceProcAddr(VkInstance instance, const char *pName);
