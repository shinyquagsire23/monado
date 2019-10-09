// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common file for the Monado SDL2 based GUI program.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#pragma once

#include "gui/gui_common.h"
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
struct sdl2_program
{
	struct gui_program base;

	bool sdl_initialized;
	SDL_Window *win;
	SDL_GLContext ctx;
};


/*!
 * Init SDL2, create and show a window and bring up any other structs needed.
 *
 * @ingroup gui
 */
int
gui_sdl2_init(struct sdl2_program *p);

/*!
 * Loop until user request quit and show Imgui interface.
 *
 * @ingroup gui
 */
void
gui_sdl2_imgui_loop(struct sdl2_program *p);

/*!
 * Loop until quit signal has been received.
 *
 * @ingroup gui
 */
void
gui_sdl2_loop(struct sdl2_program *p);

/*!
 * Destroy all SDL things and quit SDL.
 *
 * @ingroup gui
 */
void
gui_sdl2_quit(struct sdl2_program *p);


#ifdef __cplusplus
}
#endif
