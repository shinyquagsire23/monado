// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Debug helper code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_vk
 */

#include "vk/vk_helpers.h"


#ifdef VK_EXT_debug_marker

void
vk_name_object(struct vk_bundle *vk, VkDebugReportObjectTypeEXT object_type, uint64_t object, const char *name)
{
	if (!vk->has_EXT_debug_marker) {
		return;
	}

	if (object == 0) {
		U_LOG_W("Called with null object!");
		return;
	}

	VkDebugMarkerObjectNameInfoEXT name_info = {
	    .sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT,
	    .pNext = NULL,
	    .objectType = object_type,
	    .object = object,
	    .pObjectName = name,
	};

	vk->vkDebugMarkerSetObjectNameEXT(vk->device, &name_info);
}

#endif
