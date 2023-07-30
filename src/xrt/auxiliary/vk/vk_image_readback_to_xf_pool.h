// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Pool to read back VkImages from the gpu
 *
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_vk
 */

#pragma once

#include "os/os_threading.h"

#include "xrt/xrt_frame.h"
#include "xrt/xrt_handles.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_vulkan_includes.h"

#include "util/u_logging.h"
#include "util/u_string_list.h"

#include "vk/vk_helpers.h"


#define READBACK_POOL_NUM_FRAMES 16

#ifdef __cplusplus
extern "C" {
#endif


// Opaque handle
struct vk_image_readback_to_xf_pool;

struct vk_image_readback_to_xf
{
	struct xrt_frame base_frame;

	struct vk_image_readback_to_xf_pool *pool;

	VkImageLayout layout;

	VkExtent2D image_extent;
	VkImage image;
	VkDeviceMemory memory;

	bool in_use;
	bool created;
};


bool
vk_image_readback_to_xf_pool_get_unused_frame(struct vk_bundle *vk,
                                              struct vk_image_readback_to_xf_pool *pool,
                                              struct vk_image_readback_to_xf **out);

void
vk_image_readback_to_xf_pool_create(struct vk_bundle *vk,
                                    VkExtent2D extent,
                                    struct vk_image_readback_to_xf_pool **out_pool,
                                    enum xrt_format xrt_format,
                                    VkFormat vk_format);

void
vk_image_readback_to_xf_pool_destroy(struct vk_bundle *vk, struct vk_image_readback_to_xf_pool **pool_ptr);

#ifdef __cplusplus
}
#endif
