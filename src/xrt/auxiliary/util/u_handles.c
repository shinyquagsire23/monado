// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementations of handle functions
 *
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 *
 */

#include "u_handles.h"

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
#include <android/hardware_buffer.h>

static inline void
release_graphics_handle(xrt_graphics_buffer_handle_t handle)
{
	AHardwareBuffer_release(handle);
}

static inline xrt_graphics_buffer_handle_t
ref_graphics_handle(xrt_graphics_buffer_handle_t handle)
{
	AHardwareBuffer_acquire(handle);

	return handle;
}

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
#include <unistd.h>

static inline void
release_graphics_handle(xrt_graphics_buffer_handle_t handle)
{
	close(handle);
}

static inline xrt_graphics_buffer_handle_t
ref_graphics_handle(xrt_graphics_buffer_handle_t handle)
{
	return dup(handle);
}

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)

static inline void
release_graphics_handle(xrt_graphics_buffer_handle_t handle)
{
	CloseHandle(handle);
}

static inline xrt_graphics_buffer_handle_t
ref_graphics_handle(xrt_graphics_buffer_handle_t handle)
{
	HANDLE self = GetCurrentProcess();
	HANDLE result = NULL;
	if (DuplicateHandle(self, handle, self, &result, 0, FALSE, DUPLICATE_SAME_ACCESS) != 0) {
		return result;
	}
	return NULL;
}

#else
#error "need port"
#endif

xrt_graphics_buffer_handle_t
u_graphics_buffer_ref(xrt_graphics_buffer_handle_t handle)
{
	if (xrt_graphics_buffer_is_valid(handle)) {
		return ref_graphics_handle(handle);
	}

	return XRT_GRAPHICS_BUFFER_HANDLE_INVALID;
}

void
u_graphics_buffer_unref(xrt_graphics_buffer_handle_t *handle_ptr)
{
	if (handle_ptr == NULL) {
		return;
	}
	xrt_graphics_buffer_handle_t handle = *handle_ptr;
	if (!xrt_graphics_buffer_is_valid(handle)) {
		return;
	}
	release_graphics_handle(handle);
	*handle_ptr = XRT_GRAPHICS_BUFFER_HANDLE_INVALID;
}
