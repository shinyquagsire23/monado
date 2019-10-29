// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Include all of the openxr headers in one place.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

// Move these to the build system instead.
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_GRAPHICS_API_VULKAN
#define XR_USE_PLATFORM_XLIB
#define XR_USE_PLATFORM_EGL
#define XR_USE_TIMESPEC 1

#ifdef XR_USE_PLATFORM_XLIB
typedef void *Display;
typedef void *GLXFBConfig;
typedef void *GLXDrawable;
typedef void *GLXContext;
#endif

#ifdef XR_USE_PLATFORM_EGL
typedef void *EGLDisplay;
typedef void *EGLContext;
typedef void *EGLConfig;
typedef void (*__eglMustCastToProperFunctionPointerType)(void);
typedef __eglMustCastToProperFunctionPointerType (*PFNEGLGETPROCADDRESSPROC)(
    const char *procname);
#endif

#ifdef XR_USE_TIMESPEC
#include <time.h>
#endif

#include "openxr_includes/openxr.h"
#include "openxr_includes/openxr_platform.h"
#include "openxr_includes/loader_interfaces.h"
