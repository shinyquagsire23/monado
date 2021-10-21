// Copyright 2018-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Include all of the openxr headers in one place.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

//! @todo Move these to the build system instead.
#define XR_USE_TIMESPEC 1

#ifdef XR_USE_PLATFORM_ANDROID
#include <jni.h>
#endif

#ifdef XR_USE_PLATFORM_XLIB
typedef struct _XDisplay Display;
typedef void *GLXFBConfig;
typedef void *GLXDrawable;
typedef void *GLXContext;
#endif

#ifdef XR_USE_PLATFORM_WIN32
#include <Unknwn.h>
#endif

#if defined(XR_USE_PLATFORM_EGL) || defined(XR_USE_GRAPHICS_API_OPENGL_ES)
typedef void *EGLDisplay;
typedef void *EGLContext;
typedef void *EGLConfig;
typedef unsigned int EGLenum;
typedef void (*__eglMustCastToProperFunctionPointerType)(void); // NOLINT
typedef __eglMustCastToProperFunctionPointerType (*PFNEGLGETPROCADDRESSPROC)(const char *procname);
#endif

#if defined(XR_USE_GRAPHICS_API_D3D11)
#include "xrt_windows.h"
#include <d3d11.h>
#endif

#ifdef XR_USE_TIMESPEC
#include <time.h>
#endif

#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"
#include "openxr/loader_interfaces.h"
