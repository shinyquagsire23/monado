// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C++ program part for the SDL test.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup sdl_test
 */

#include "ogl/ogl_api.h"

#include "util/u_misc.h"

#include "sdl_internal.hpp"


void
sdl_create_window(struct sdl_program *sp)
{
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		assert(false);
	}

	char title[1024];
	snprintf(title, sizeof(title), "Monado! ☃");

	int x = SDL_WINDOWPOS_UNDEFINED;
	int y = SDL_WINDOWPOS_UNDEFINED;
	int w = 1920;
	int h = 1080;

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	int window_flags = 0;
	window_flags |= SDL_WINDOW_SHOWN;
	window_flags |= SDL_WINDOW_OPENGL;
	window_flags |= SDL_WINDOW_RESIZABLE;
	window_flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#if 0
	window_flags |= SDL_WINDOW_MAXIMIZED;
#endif


	sp->win = SDL_CreateWindow(title, x, y, w, h, window_flags);

	if (sp->win == NULL) {
		assert(false);
	}

	sp->ctx = SDL_GL_CreateContext(sp->win);
	if (sp->ctx == NULL) {
		assert(false);
	}

	// Make the context current in this thread for loading OpenGL.
	sdl_make_current(sp);
	SDL_GL_SetSwapInterval(1); // Enable vsync

	// Setup OpenGL bindings.
	bool err = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress) == 0;
	if (err) {
		assert(false);
	}

	// We are going to render on a different thread, make sure to unbind it.
	sdl_make_uncurrent(sp);
}

extern "C" struct sdl_program *
sdl_program_plus_create()
{
	sdl_program_plus &spp = *new sdl_program_plus();
	spp.spp = &spp;

	os_mutex_init(&spp.current_mutex);

	// Initial state.
	spp.log_level = U_LOGGING_INFO;
	spp.state.head.pose = XRT_POSE_IDENTITY;

	// Create the window, init before sub components.
	sdl_create_window(&spp);

	// Init sub components.
	sdl_instance_init(&spp);
	sdl_system_devices_init(&spp);
	sdl_device_init(&spp);
	sdl_compositor_init(&spp); // Needs the window.

	return &spp;
}

extern "C" void
sdl_program_plus_render(struct sdl_program_plus *spp_ptr)
{
	auto &spp = *spp_ptr;

	// Make context current
	sdl_make_current(&spp);

	// Flush the events.
	SDL_Event e = {0};
	while (SDL_PollEvent(&e)) {
		// Nothing for now.
	}

	if (spp.c.base.slot.layer_count == 0) {
		glClearColor(0.2f, 0.2f, 0.2f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	} else if (spp.c.base.slot.layers[0].data.type == XRT_LAYER_STEREO_PROJECTION ||
	           spp.c.base.slot.layers[0].data.type == XRT_LAYER_STEREO_PROJECTION_DEPTH) {

		auto &l = spp.c.base.slot.layers[0];
		auto &ssc = *(sdl_swapchain *)l.sc_array[0];
		GLuint tex = ssc.textures[l.data.stereo.l.sub.image_index];

		glClearColor(0.2f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		GLuint fbo = 0;
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		CHECK_GL();

		glFramebufferTexture2D(   //
		    GL_READ_FRAMEBUFFER,  // GLenum target
		    GL_COLOR_ATTACHMENT0, // GLenum attachment
		    GL_TEXTURE_2D,        // GLenum textarget
		    tex,                  // GLuint texture
		    0);                   // GLint level
		CHECK_GL();

		int w, h;
		SDL_GetWindowSize(spp.win, &w, &h);
		glBlitFramebuffer(       //
		    0,                   // GLint srcX0
		    0,                   // GLint srcY0
		    ssc.w,               // GLint srcX1
		    ssc.h,               // GLint srcY1
		    0,                   // GLint dstX0
		    0,                   // GLint dstY0
		    w,                   // GLint dstX1
		    h,                   // GLint dstY1
		    GL_COLOR_BUFFER_BIT, // GLbitfield mask
		    GL_NEAREST);         // GLenum filter
		CHECK_GL();

		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		CHECK_GL();

		glDeleteFramebuffers(1, &fbo);
	} else {
		glClearColor(1.0f, 0.0f, 1.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	// Display what we rendered.
	SDL_GL_SwapWindow(spp.win);

	// Will be used when creating swapchains, unbind it.
	sdl_make_uncurrent(&spp);
}

extern "C" void
sdl_program_plus_destroy(struct sdl_program_plus *spp)
{
	os_mutex_destroy(&spp->current_mutex);

	delete spp;
}
