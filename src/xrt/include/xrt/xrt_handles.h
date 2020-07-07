// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Native handle types.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

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
 * The type underlying buffers shared between compositor clients and the main
 * compositor.
 *
 * On Linux, this is a file descriptor.
 */
typedef int xrt_graphics_buffer_handle_t;

#ifdef __cplusplus
}
#endif
