// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining a XRT graphics provider.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef void* Display;
typedef void* GLXFBConfig;
typedef void* GLXDrawable;
typedef void* GLXContext;

/*!
 * @ingroup xrt_iface
 */
struct xrt_compositor_gl*
xrt_gfx_provider_create_gl_xlib(struct xrt_device* xdev,
                                Display* xDisplay,
                                uint32_t visualid,
                                GLXFBConfig glxFBConfig,
                                GLXDrawable glxDrawable,
                                GLXContext glxContext);


#ifdef __cplusplus
}
#endif
