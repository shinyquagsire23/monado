# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(
	aux_ogl STATIC
	ogl_documentation.h
	ogl_api.c
	ogl_api.h
	ogl_helpers.c
	ogl_helpers.h
	)
target_link_libraries(aux_ogl PUBLIC aux-includes xrt-external-glad)

if(XRT_HAVE_OPENGL_GLX AND XRT_HAVE_XLIB)
	target_sources(aux_ogl PRIVATE glx_api.c glx_api.h)
endif()

if(XRT_HAVE_EGL)
	target_sources(aux_ogl PRIVATE egl_api.c egl_api.h)
endif()

if(XRT_HAVE_OPENGLES)
	target_link_libraries(aux_ogl PUBLIC EGL::EGL)
endif()

if(WIN32)
	target_sources(aux_ogl PRIVATE wgl_api.c wgl_api.h)
	target_link_libraries(aux_ogl PUBLIC OpenGL::GL)
endif()
