// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining a XRT graphics provider.
 * @author Drew DeVault <sir@cmpwn.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void (*__eglMustCastToProperFunctionPointerType)(void); // NOLINT
typedef __eglMustCastToProperFunctionPointerType (*PFNEGLGETPROCADDRESSPROC)(const char *proc);
struct time_state;

/*!
 * Create an OpenGL(ES) compositor client using EGL.
 *
 * @ingroup xrt_iface
 * @public @memberof xrt_compositor_native
 */
xrt_result_t
xrt_gfx_provider_create_gl_egl(struct xrt_compositor_native *xcn,
                               EGLDisplay display,
                               EGLConfig config,
                               EGLContext context,
                               PFNEGLGETPROCADDRESSPROC get_gl_procaddr,
                               struct xrt_compositor_gl **out_xcgl);

#ifdef __cplusplus
}
#endif
