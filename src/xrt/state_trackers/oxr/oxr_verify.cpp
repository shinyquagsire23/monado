// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  File for verifing app input into api functions.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 * @ingroup oxr_api
 */

#include <stdio.h>

#include "xrt/xrt_compiler.h"
#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_api_verify.h"


/*
 *
 * Path verification.
 *
 */

static bool
contains_zero(const char* path, uint32_t size)
{
	for (uint32_t i = 0; i < size; i++) {
		if (path[i] == '\0') {
			return true;
		}
	}

	return false;
}

extern "C" XrResult
oxr_verify_fixed_size_single_level_path(struct oxr_logger* log,
                                        const char* path,
                                        uint32_t size,
                                        const char* name)
{
	if (size == 0) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(%s) internal runtime error", name);
	}

	if (path[0] == '\0') {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(%s) can not be empty", name);
	}

	if (!contains_zero(path, size)) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(%s) must include zero termination '\\0'.",
		                 name);
	}

	//! @todo verify more!

	return XR_SUCCESS;
}


/*
 *
 * Other verification.
 *
 */

extern "C" XrResult
oxr_verify_XrSessionCreateInfo(struct oxr_logger* log,
                               const struct oxr_instance* inst,
                               const XrSessionCreateInfo* createInfo)
{
	if (createInfo->type != XR_TYPE_SESSION_CREATE_INFO) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "createInfo->type");
	}

	if (createInfo->next == NULL) {
		if (inst->headless) {
			return XR_SUCCESS;
		}
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "createInfo->next");
	}

	XrStructureType* next_type = (XrStructureType*)createInfo->next;
#ifdef XR_USE_PLATFORM_XLIB
	if (*next_type == XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR) {
		if (!inst->opengl_enable) {
			return oxr_error(
			    log, XR_ERROR_VALIDATION_FAILURE,
			    "OpenGL "
			    "requires " XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
		}
		return oxr_verify_XrGraphicsBindingOpenGLXlibKHR(
		    log, (XrGraphicsBindingOpenGLXlibKHR*)createInfo->next);
	} else
#endif
#ifdef XR_USE_GRAPHICS_API_VULKAN
	if (*next_type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) {
		if (!inst->vulkan_enable) {
			return oxr_error(
			    log, XR_ERROR_VALIDATION_FAILURE,
			    "Vulkan "
			    "requires " XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
		}
		return oxr_verify_XrGraphicsBindingVulkanKHR(
		    log, (XrGraphicsBindingVulkanKHR*)createInfo->next);
	} else
#endif
	{
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "createInfo->next->type");
	}

	return XR_SUCCESS;
}


#ifdef XR_USE_PLATFORM_XLIB

extern "C" XrResult
oxr_verify_XrGraphicsBindingOpenGLXlibKHR(
    struct oxr_logger* log, const XrGraphicsBindingOpenGLXlibKHR* next)
{
	if (next->type != XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "createInfo->next->type");
	}

	if (next->next != NULL) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "createInfo->next->next");
	}

	return XR_SUCCESS;
}

#endif


#ifdef XR_USE_GRAPHICS_API_VULKAN

extern "C" XrResult
oxr_verify_XrGraphicsBindingVulkanKHR(struct oxr_logger* log,
                                      const XrGraphicsBindingVulkanKHR* next)
{
	if (next->type != XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "createInfo->next->type");
	}

	if (next->next != NULL) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "createInfo->next->next");
	}

	return XR_SUCCESS;
}

#endif
