// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common file for the Monado GUI program.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#pragma once

#include "xrt/xrt_defines.h"
#include <SDL2/SDL.h>

/*!
 * @defgroup gui GUI Config Interface
 * @ingroup xrt
 *
 * @brief Small GUI interface to configure Monado based on SDL2.
 */


#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Common struct holding state for the GUI interface.
 *
 * @ingroup gui
 */
struct program
{
	SDL_Window *win;
	SDL_GLContext ctx;

	bool stopped;
	bool initialized;

	struct
	{
		SDL_Surface *sf;
		uint8_t *buffer;
		size_t stride;
		uint32_t width;
		uint32_t height;
		bool own_buffer;
	} blit;
};

/*!
 * Init SDL2, create and show a window and bring up any other structs needed.
 *
 * @ingroup gui
 */
int
gui_sdl2_init(struct program *p);

/*!
 * Loop until quit signal has been received.
 *
 * @ingroup gui
 */
void
gui_sdl2_loop(struct program *p);

/*!
 * Display a 24bit RGB image on the screen.
 *
 * @ingroup gui
 */
void
gui_sdl2_display_R8G8B8(struct program *p,
                        bool resize,
                        uint32_t width,
                        uint32_t height,
                        size_t stride,
                        void *data);

/*!
 * Destroy all SDL things and quit SDL.
 *
 * @ingroup gui
 */
void
gui_sdl2_quit(struct program *p);


#ifdef __cplusplus
}
#endif
