// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Native handle types.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt_config_os.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * The type for shared memory blocks shared over IPC.
 *
 * On Linux, this is a file descriptor.
 */
typedef int xrt_shmem_handle_t;

/*!
 * Check whether a shared memory handle is valid.
 *
 * @public @memberof xrt_shmem_handle_t
 */
static inline bool
xrt_shmem_is_valid(xrt_shmem_handle_t handle)
{
	return handle >= 0;
}

/*!
 * An invalid value for a shared memory block.
 *
 * Note that there may be more than one value that's invalid - use
 * @ref xrt_shmem_is_valid instead of comparing against this!
 *
 * @relates xrt_shmem_handle_t
 */
#define XRT_SHMEM_HANDLE_INVALID (-1)

#if defined(XRT_OS_ANDROID)
typedef struct AHardwareBuffer AHardwareBuffer;

/*!
 * The type underlying buffers shared between compositor clients and the main
 * compositor.
 *
 * On Android, this is an @p AHardwareBuffer pointer.
 */
typedef AHardwareBuffer *xrt_graphics_buffer_handle_t;

/*!
 * Check whether a graphics buffer handle is valid.
 *
 * @public @memberof xrt_graphics_buffer_handle_t
 */
static inline bool
xrt_graphics_buffer_is_valid(xrt_graphics_buffer_handle_t handle)
{
	return handle != NULL;
}

/*!
 * An invalid value for a graphics buffer.
 *
 * Note that there may be more than one value that's invalid - use
 * xrt_graphics_buffer_is_valid() instead of comparing against this!
 *
 * @relates xrt_graphics_buffer_handle_t
 */
#define XRT_GRAPHICS_BUFFER_HANDLE_INVALID NULL

#else

/*!
 * The type underlying buffers shared between compositor clients and the main
 * compositor.
 *
 * On Linux, this is a file descriptor.
 */
typedef int xrt_graphics_buffer_handle_t;

/*!
 * Check whether a graphics buffer handle is valid.
 *
 * @public @memberof xrt_graphics_buffer_handle_t
 */
static inline bool
xrt_graphics_buffer_is_valid(xrt_graphics_buffer_handle_t handle)
{
	return handle >= 0;
}

/*!
 * An invalid value for a graphics buffer.
 *
 * Note that there may be more than one value that's invalid - use
 * xrt_graphics_buffer_is_valid() instead of comparing against this!
 *
 * @relates xrt_graphics_buffer_handle_t
 */
#define XRT_GRAPHICS_BUFFER_HANDLE_INVALID (-1)

#endif

#ifdef __cplusplus
}
#endif
