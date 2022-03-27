// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan code for compositors.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_util
 */

#pragma once

#include "xrt/xrt_compositor.h"
#include "util/u_logging.h"
#include "util/u_string_list.h"
#include "vk/vk_helpers.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Arguments to Vulkan bundle initialisation, all args needs setting.
 */
struct comp_vulkan_arguments
{
	//! Vulkan version that is required.
	uint32_t required_instance_version;

	//! Function to get all Vulkan functions from.
	PFN_vkGetInstanceProcAddr get_instance_proc_address;

	//! Extensions that the instance is created with.
	struct u_string_list *required_instance_extensions;

	//! Extensions that the instance is created with.
	struct u_string_list *optional_instance_extensions;

	//! Extensions that the device is created with.
	struct u_string_list *required_device_extensions;

	//! Extensions that the device is created with.
	struct u_string_list *optional_device_extensions;

	//! Logging level to be set on the @ref vk_bundle.
	enum u_logging_level log_level;

	//! Should we look for a queue with no graphics, only compute.
	bool only_compute_queue;

	//! Should we try to enable timeline semaphores if available
	bool timeline_semaphore;

	//! Vulkan physical device to be selected, -1 for auto.
	int selected_gpu_index;

	//! Vulkan physical device index for clients to use, -1 for auto.
	int client_gpu_index;
};

/*!
 * Extra results from Vulkan bundle initialisation.
 */
struct comp_vulkan_results
{
	//! Vulkan physical device selected.
	int selected_gpu_index;

	//! Vulkan physical device index for clients to use.
	int client_gpu_index;

	//! Selected Vulkan device UUID.
	uint8_t selected_gpu_deviceUUID[XRT_GPU_UUID_SIZE];

	//! Selected Vulkan device UUID to suggest to clients.
	uint8_t client_gpu_deviceUUID[XRT_GPU_UUID_SIZE];
};

/*!
 * Fully initialises a @ref vk_bundle, by creating instance, device and queue.
 *
 * @ingroup comp_util
 */
bool
comp_vulkan_init_bundle(struct vk_bundle *vk,
                        const struct comp_vulkan_arguments *vk_args,
                        struct comp_vulkan_results *vk_res);


/*
 *
 * Format checking.
 *
 */

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
 * 10 bits are not considered enough to do linear colours without
 * banding. If there was a sRGB variant of it then we would have used it
 * instead but there isn't. Since it's not a popular format it's best
 * not to list it rather then listing it and people falling into the
 * trap. The absolute minimum is R11G11B10, but is a really weird format
 * so we are not exposing it.

 * @ingroup comp_util
 */
#define COMP_VULKAN_FORMATS(THING_COLOR, THING_DS)                                                                     \
	/* color formats */                                                                                            \
	THING_COLOR(R16G16B16A16_UNORM)  /* OGL VK */                                                                  \
	THING_COLOR(R16G16B16A16_SFLOAT) /* OGL VK */                                                                  \
	THING_COLOR(R16G16B16_UNORM)     /* OGL VK - Uncommon. */                                                      \
	THING_COLOR(R16G16B16_SFLOAT)    /* OGL VK - Uncommon. */                                                      \
	THING_COLOR(R8G8B8A8_SRGB)       /* OGL VK */                                                                  \
	THING_COLOR(B8G8R8A8_SRGB)       /* VK */                                                                      \
	THING_COLOR(R8G8B8_SRGB)         /* OGL VK - Uncommon. */                                                      \
	THING_COLOR(R8G8B8A8_UNORM)      /* OGL VK - Bad colour precision. */                                          \
	THING_COLOR(B8G8R8A8_UNORM)      /* VK     - Bad colour precision. */                                          \
	THING_COLOR(R8G8B8_UNORM)        /* OGL VK - Uncommon. Bad colour precision. */                                \
	THING_COLOR(B8G8R8_UNORM)        /* VK     - Uncommon. Bad colour precision. */                                \
	/* depth formats */                                                                                            \
	THING_DS(D16_UNORM)  /* OGL VK */                                                                              \
	THING_DS(D32_SFLOAT) /* OGL VK */                                                                              \
	/* depth stencil formats */                                                                                    \
	THING_DS(D24_UNORM_S8_UINT)  /* OGL VK */                                                                      \
	THING_DS(D32_SFLOAT_S8_UINT) /* OGL VK */

/*!
 * Struct with supported format, these are not only check for optimal flags
 * but also the ability to import and export them.
 */
struct comp_vulkan_formats
{
#define FIELD(IDENT) bool has_##IDENT;
	COMP_VULKAN_FORMATS(FIELD, FIELD)
#undef FIELD
};

/*!
 * Fills in a @ref comp_vulkan_formats struct with the supported formats, use
 * @ref comp_vulkan_formats_copy_to_info to fill a compositor info struct.
 *
 * @ingroup comp_util
 */
void
comp_vulkan_formats_check(struct vk_bundle *vk, struct comp_vulkan_formats *formats);

/*!
 * Fills in a @ref xrt_compositor_info struct with the formats listed from a
 * @ref comp_vulkan_formats. This and @ref comp_vulkan_formats_check are split
 * to allow the compositor to allow/deny certain formats.
 *
 * @ingroup comp_util
 */
void
comp_vulkan_formats_copy_to_info(const struct comp_vulkan_formats *formats, struct xrt_compositor_info *info);

/*!
 * Logs the formats at info level.
 *
 * @ingroup comp_util
 */
void
comp_vulkan_formats_log(enum u_logging_level log_level, const struct comp_vulkan_formats *formats);


#ifdef __cplusplus
}
#endif
