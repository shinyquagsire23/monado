// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Header exposing factory function for AHardwareBuffer backed image
   buffer allocator.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

#pragma once

#include "xrt/xrt_compositor.h"
#include "xrt/xrt_handles.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER

struct xrt_image_native_allocator *
android_ahardwarebuffer_allocator_create();

#endif // XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER

#ifdef __cplusplus
}
#endif
