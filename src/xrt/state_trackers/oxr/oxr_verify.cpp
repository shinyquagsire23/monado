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

#include <cstdio>
#include <cstring>

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
valid_path_char(const char c)
{
	if ('a' <= c && c <= 'z') {
		return true;
	}

	if ('0' <= c && c <= '9') {
		return true;
	}

	if (c == '-' || c == '_' || c == '.' || c == '/') {
		return true;
	}

	return false;
}

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
                                        uint32_t array_size,
                                        const char* name)
{
	if (array_size == 0) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "(%s) internal runtime error", name);
	}

	if (path[0] == '\0') {
		return oxr_error(log, XR_ERROR_PATH_FORMAT_INVALID,
		                 "(%s) can not be empty", name);
	}

	if (!contains_zero(path, array_size)) {
		return oxr_error(log, XR_ERROR_PATH_FORMAT_INVALID,
		                 "(%s) must include zero termination '\\0'.",
		                 name);
	}

	size_t length = strlen(path);
	for (size_t i = 0; i < length; i++) {
		const char c = path[i];

		// Slashes are not valid in single level paths.
		if (valid_path_char(c) && c != '/') {
			continue;
		}

		return oxr_error(
		    log, XR_ERROR_PATH_FORMAT_INVALID,
		    "(%s) 0x%02x is not a valid character at position %u", name,
		    c, (uint32_t)i);
	}

	return XR_SUCCESS;
}

enum class State
{
	Start,
	Middle,
	Slash,
	SlashDots,
};

extern "C" XrResult
oxr_verify_full_path_c(struct oxr_logger* log,
                       const char* path,
                       const char* name)
{
	size_t length = strlen(path);

	if (length >= UINT32_MAX) {
		return oxr_error(log, XR_ERROR_PATH_FORMAT_INVALID,
		                 "(%s) path to long", name);
	}

	return oxr_verify_full_path(log, path, (uint32_t)length, name);
}

extern "C" XrResult
oxr_verify_full_path(struct oxr_logger* log,
                     const char* path,
                     size_t length,
                     const char* name)
{
	State state = State::Start;
	bool valid = true;

	if (length >= UINT32_MAX || (length + 1) > XR_MAX_PATH_LENGTH) {
		return oxr_error(
		    log, XR_ERROR_PATH_FORMAT_INVALID,
		    "(%s) string is too long for a path (%u + 1) > %u", name,
		    (uint32_t)length, XR_MAX_PATH_LENGTH);
	}

	for (uint32_t i = 0; i < length; i++) {
		const char c = path[i];
		switch (state) {
		case State::Start:
			if (c != '/') {
				return oxr_error(log,
				                 XR_ERROR_PATH_FORMAT_INVALID,
				                 "(%s) does not start with a "
				                 "fowrward slash",
				                 name);
			}
			state = State::Slash;
			break;
		case State::Slash:
			switch (c) {
			case '.':
				// Is valid and starts the SlashDot(s) state.
				state = State::SlashDots;
				break;
			case '/':
				return oxr_error(
				    log, XR_ERROR_PATH_FORMAT_INVALID,
				    "(%s) '//' is not a valid in a path", name);
			default:
				valid = valid_path_char(c);
				state = State::Middle;
			}
			break;
		case State::Middle:
			switch (c) {
			case '/': state = State::Slash; break;
			default:
				valid = valid_path_char(c);
				state = State::Middle;
			}
			break;
		case State::SlashDots:
			switch (c) {
			case '/':
				return oxr_error(
				    log, XR_ERROR_PATH_FORMAT_INVALID,
				    "(%s) '/.[.]*/' is not a valid in a path",
				    name);
			case '.':
				// It's valid, more ShashDot(s).
				break;
			default:
				valid = valid_path_char(c);
				state = State::Middle;
			}
			break;
		}

		if (!valid) {
			return oxr_error(log, XR_ERROR_PATH_FORMAT_INVALID,
			                 "(%s) 0x%02x is not a valid character "
			                 "at position %u",
			                 name, c, (uint32_t)length);
		}
	}

	switch (state) {
	case State::Start:
		// Empty string
		return oxr_error(log, XR_ERROR_PATH_FORMAT_INVALID,
		                 "(%s) a empty string is not a valid path",
		                 name);
	case State::Slash:
		// Is this '/foo/' or '/'
		if (length > 1) {
			// It was '/foo/'
			return XR_SUCCESS;
		}
		// It was '/'
		return oxr_error(log, XR_ERROR_PATH_FORMAT_INVALID,
		                 "(%s) the string '%s' is not a valid path",
		                 name, path);
	case State::SlashDots:
		// Does the path ends with '/..'
		return oxr_error(
		    log, XR_ERROR_PATH_FORMAT_INVALID,
		    "(%s) strings ending with '/.[.]*' is not a valid", name);

	case State::Middle:
		// '/foo/bar' okay!
		return XR_SUCCESS;
	default:
		// We should not end up here.
		return oxr_error(
		    log, XR_ERROR_RUNTIME_FAILURE,
		    "(%s) internal runtime error validating path (%s)", name,
		    path);
	}
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
		return oxr_error(log, XR_ERROR_GRAPHICS_DEVICE_INVALID,
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
