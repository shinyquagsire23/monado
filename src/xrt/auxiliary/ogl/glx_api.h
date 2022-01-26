// Copyright 2022, James Hogan <james@albanarts.com>
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  GLX API wrapper header.
 * @author James Hogan <james@albanarts.com>
 * @ingroup aux_ogl
 */

#pragma once

#include "xrt/xrt_gfx_xlib.h"

// We don't #include "glad/glx.h" here in order not to conflict with our typedefs in xrt_gfx_xlib.h.

Display *
glXGetCurrentDisplay(void);
GLXContext
glXGetCurrentContext(void);
GLXDrawable
glXGetCurrentDrawable(void);
GLXDrawable
glXGetCurrentReadDrawable(void);
bool
glXMakeContextCurrent(Display *dpy, GLXDrawable draw, GLXDrawable read, GLXContext ctx);
bool
glXMakeCurrent(Display *dpy, GLXDrawable drawable, GLXContext ctx);

typedef void (*GLADapiproc)(void);
typedef GLADapiproc (*GLADloadfunc)(const char *name);
int
gladLoadGLX(Display *display, int screen, GLADloadfunc load);
