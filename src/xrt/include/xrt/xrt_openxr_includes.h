// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Include all of the openxr headers in one place.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include <xrt/xrt_config_have.h>

//! @todo Move these to the build system instead.
#define XR_USE_TIMESPEC 1

#ifdef XRT_HAVE_VULKAN
#define XR_USE_GRAPHICS_API_VULKAN
#endif

#ifdef XR_USE_PLATFORM_ANDROID
#include <jni.h>
#endif

#ifdef XR_USE_PLATFORM_XLIB
typedef struct _XDisplay Display;
typedef void *GLXFBConfig;
typedef void *GLXDrawable;
typedef void *GLXContext;
#endif

#if defined(XR_USE_PLATFORM_EGL) || defined(XR_USE_GRAPHICS_API_OPENGL_ES)
typedef void *EGLDisplay;
typedef void *EGLContext;
typedef void *EGLConfig;
typedef void (*__eglMustCastToProperFunctionPointerType)(void);
typedef __eglMustCastToProperFunctionPointerType (*PFNEGLGETPROCADDRESSPROC)(const char *procname);
#endif

#ifdef XR_USE_TIMESPEC
#include <time.h>
#endif

#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"
#include "openxr/loader_interfaces.h"
