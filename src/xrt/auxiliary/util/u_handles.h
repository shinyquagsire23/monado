// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for dealing generically with various handle types defined
 * in xrt_handles.h
 *
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 *
 */

#pragma once

#include <xrt/xrt_handles.h>

#ifdef __cplusplus
extern "C" {
#endif
/*!
 * Increase the reference count on the buffer handle, returning the new
 * reference.
 *
 * Depending on the underlying type, the value may be the same or different than
 * what was passed in. It should be retained for use at release time,
 * regardless.
 *
 * @public @memberof xrt_graphics_buffer_handle_t
 */
xrt_graphics_buffer_handle_t
u_graphics_buffer_ref(xrt_graphics_buffer_handle_t handle);

/*!
 * Decrease the reference count/release the handle reference passed in.
 *
 * Be sure to only call this once per handle.
 *
 * Performs null-check and clears the value after unreferencing.
 *
 * @public @memberof xrt_graphics_buffer_handle_t
 */
void
u_graphics_buffer_unref(xrt_graphics_buffer_handle_t *handle);

#ifdef __cplusplus
} // extern "C"
#endif
