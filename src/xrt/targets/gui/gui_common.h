// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common file for the Monado GUI program.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#pragma once

#include "xrt/xrt_frame.h"
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

#define NUM_XDEVS 8
struct xrt_device;
struct xrt_prober;
struct time_state;
struct gui_scene_manager;

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

	struct gui_scene_manager *gsm;

	struct
	{
		SDL_Surface *sf;
		uint8_t *buffer;
		size_t stride;
		uint32_t width;
		uint32_t height;
		bool own_buffer;
	} blit;

	struct time_state *timekeeping;
	struct xrt_device *xdevs[NUM_XDEVS];
	struct xrt_prober *xp;

	struct gui_ogl_texture *texs[256];
};

/*!
 * A single currently running scene.
 */
struct gui_scene
{
	void (*render)(struct gui_scene *, struct program *);
	void (*destroy)(struct gui_scene *);
};

/*!
 * A OpenGL texture.
 *
 * @ingroup gui
 */
struct gui_ogl_texture
{
	uint64_t seq;
	uint64_t dropped;
	const char *name;
	uint32_t w, h;
	uint32_t id;
	bool half;
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

/*!
 * Initialize the prober and open all devices found.
 *
 * @ingroup gui
 */
int
gui_prober_init(struct program *p);

/*!
 * Update all devices.
 *
 * @ingroup gui
 */
void
gui_prober_update(struct program *p);

/*!
 * Destroy all opened devices and destroy the prober.
 *
 * @ingroup gui
 */
void
gui_prober_teardown(struct program *p);

/*!
 * Create a sink that will turn frames into OpenGL textures, since the frame
 * can come from another thread @ref gui_ogl_sink_update needs to be called.
 *
 * Destruction is handled by the frame context.
 *
 * @ingroup gui
 */
struct gui_ogl_texture *
gui_ogl_sink_create(const char *name,
                    struct xrt_frame_context *xfctx,
                    struct xrt_frame_sink **out_sink);

/*!
 * Update the texture to the latest received frame.
 *
 * @ingroup gui
 */
void
gui_ogl_sink_update(struct gui_ogl_texture *);

/*!
 * Push the scene to the top of the lists.
 *
 * @ingroup gui
 */
void
gui_scene_push_front(struct program *p, struct gui_scene *me);

/*!
 * Put a scene on the delete list, also removes it from any other list.
 *
 * @ingroup gui
 */
void
gui_scene_delete_me(struct program *p, struct gui_scene *me);

/*!
 * Render the scenes.
 *
 * @ingroup gui
 */
void
gui_scene_manager_render(struct program *p);

/*!
 * Initialize the scene manager.
 *
 * @ingroup gui
 */
void
gui_scene_manager_init(struct program *p);

/*!
 * Destroy the scene manager.
 *
 * @ingroup gui
 */
void
gui_scene_manager_destroy(struct program *p);


#ifdef __cplusplus
}
#endif
