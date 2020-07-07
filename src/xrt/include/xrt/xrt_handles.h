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

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * The type for shared memory blocks shared over IPC.
 *
 * On Linux, this is a file descriptor.
 */
typedef int xrt_shmem_handle_t;

#if defined(XRT_OS_ANDROID)
typedef struct AHardwareBuffer AHardwareBuffer;

typedef AHardwareBuffer *xrt_graphics_buffer_handle_t;
#else

/*!
 * The type underlying buffers shared between compositor clients and the main
 * compositor.
 *
 * On Linux, this is a file descriptor.
 */
typedef int xrt_graphics_buffer_handle_t;
#endif

#ifdef __cplusplus
}
#endif
