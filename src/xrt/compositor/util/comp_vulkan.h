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
	xrt_uuid_t selected_gpu_deviceUUID;

	//! Selected Vulkan device UUID to suggest to clients.
	xrt_uuid_t client_gpu_deviceUUID;

	//! The (Windows) LUID for the GPU device suggested for clients.
	xrt_luid_t client_gpu_deviceLUID;

	//! Whether @ref client_gpu_deviceLUID is valid (probably only on Windows)
	bool client_gpu_deviceLUID_valid;
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
 * Struct with supported format, these are not only check for optimal flags
 * but also the ability to import and export them.
 */
struct comp_vulkan_formats
{
#define FIELD(IDENT) bool has_##IDENT;
	VK_CSCI_FORMATS(FIELD, FIELD, FIELD, FIELD)
#undef FIELD

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	//! Is VK_FORMAT_R8G8B8A8_SRGB emulated with VK_FORMAT_R8G8B8A8_UNORM?
	bool emulated_R8G8B8A8_SRGB;
#endif
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
