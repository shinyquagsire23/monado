// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  SDL2 functions to drive the GUI.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "gui_common.h"
#include "glad/gl.h"


/*
 *
 * Internal static functions
 *
 */

XRT_MAYBE_UNUSED static void
create_blit_info(struct program *p, uint32_t width, uint32_t height)
{
	if (p->blit.sf != NULL) {
		SDL_FreeSurface(p->blit.sf);
		p->blit.sf = NULL;

		if (p->blit.own_buffer) {
			free(p->blit.buffer);
		}
		p->blit.buffer = NULL;
	}

	size_t stride = width * 4;
	size_t size = stride * height;
	p->blit.width = width;
	p->blit.height = height;
	p->blit.stride = stride;
	p->blit.own_buffer = true;

	p->blit.buffer = malloc(size);
	p->blit.sf =
	    SDL_CreateRGBSurfaceFrom(p->blit.buffer, width, height, 32, stride,
	                             0x0000FF, 0x00FF00, 0xFF0000, 0);
}

static void
sdl2_handle_keydown(struct program *p, const SDL_Event *e)
{
	switch (e->key.keysym.sym) {
	case SDLK_ESCAPE: p->stopped = true; break;
	default: break;
	}

	return;
}


/*
 *
 * "Exported" functions.
 *
 */

void
gui_sdl2_display_R8G8B8(struct program *p,
                        bool resize,
                        uint32_t width,
                        uint32_t height,
                        size_t stride,
                        void *data)
{
	if (p->blit.buffer != data || p->blit.width != width ||
	    p->blit.height != height || p->blit.sf == NULL) {

		if (resize) {
			SDL_SetWindowSize(p->win, width, height);
		}

		p->blit.sf =
		    SDL_CreateRGBSurfaceFrom(data, width, height, 24, stride,
		                             0x0000FF, 0x00FF00, 0xFF0000, 0);

		p->blit.width = width;
		p->blit.height = height;
		p->blit.stride = stride;
		p->blit.buffer = data;
		p->blit.own_buffer = false;
	}

	SDL_Surface *win_sf = SDL_GetWindowSurface(p->win);

	if (SDL_BlitSurface(p->blit.sf, NULL, win_sf, NULL) == 0) {
		SDL_UpdateWindowSurface(p->win);
	}
}

void
gui_sdl2_loop(struct program *p)
{
	SDL_Event event;
	while (!p->stopped) {
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

	return;
}

int
gui_sdl2_init(struct program *p)
{
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		return -1;
	}

	const char *title = "Monado! ☺";
	int x = SDL_WINDOWPOS_UNDEFINED;
	int y = SDL_WINDOWPOS_UNDEFINED;
	int w = 1920;
	int h = 1080;

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
	                    SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	SDL_WindowFlags window_flags = 0;
	window_flags |= SDL_WINDOW_SHOWN;
	window_flags |= SDL_WINDOW_OPENGL;
	window_flags |= SDL_WINDOW_RESIZABLE;
	window_flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#if 0
	window_flags |= SDL_WINDOW_MAXIMIZED;
#endif

	p->initialized = true;
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
gui_sdl2_quit(struct program *p)
{
	if (!p->initialized) {
		return;
	}

	if (p->blit.sf != NULL) {
		SDL_FreeSurface(p->blit.sf);
		p->blit.sf = NULL;

		if (p->blit.own_buffer) {
			free(p->blit.buffer);
		}
		p->blit.buffer = NULL;
	}

	if (p->ctx != NULL) {
		SDL_GL_DeleteContext(p->ctx);
		p->ctx = NULL;
	}

	if (p->win != NULL) {
		SDL_DestroyWindow(p->win);
		p->win = NULL;
	}

	SDL_Quit();
	p->initialized = false;
}
