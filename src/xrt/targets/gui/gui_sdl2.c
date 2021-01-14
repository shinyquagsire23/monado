// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  SDL2 functions to drive the GUI.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "ogl/ogl_api.h"
#include "gui_sdl2.h"


/*
 *
 * Internal static functions
 *
 */

static void
sdl2_handle_keydown(struct sdl2_program *p, const SDL_Event *e)
{
	switch (e->key.keysym.sym) {
	case SDLK_ESCAPE: p->base.stopped = true; break;
	default: break;
	}
}


/*
 *
 * "Exported" functions.
 *
 */

void
gui_sdl2_loop(struct sdl2_program *p)
{
	SDL_Event event;
	while (!p->base.stopped) {
		if (SDL_WaitEvent(&event) == 0) {
			return;
		}

		if (event.type == SDL_QUIT) {
			return;
		}

		if (event.type == SDL_KEYDOWN) {
			sdl2_handle_keydown(p, &event);
		}
	}
}

int
gui_sdl2_init(struct sdl2_program *p)
{
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		return -1;
	}
	p->sdl_initialized = true;

	const char *title = "Monado! ☺";
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


	p->win = SDL_CreateWindow(title, x, y, w, h, window_flags);

	if (p->win == NULL) {
		return -1;
	}

	p->ctx = SDL_GL_CreateContext(p->win);
	if (p->ctx == NULL) {
		return -1;
	}

	SDL_GL_MakeCurrent(p->win, p->ctx);
	SDL_GL_SetSwapInterval(1); // Enable vsync

	// Setup OpenGL bindings.
	bool err = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress) == 0;
	if (err) {
		return -1;
	}

	return 0;
}

void
gui_sdl2_quit(struct sdl2_program *p)
{
	if (p->ctx != NULL) {
		SDL_GL_DeleteContext(p->ctx);
		p->ctx = NULL;
	}

	if (p->win != NULL) {
		SDL_DestroyWindow(p->win);
		p->win = NULL;
	}

	SDL_Quit();
	p->sdl_initialized = false;
}
