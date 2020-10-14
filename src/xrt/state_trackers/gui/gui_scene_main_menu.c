// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main menu.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "util/u_misc.h"

#include "gui_common.h"
#include "gui_imgui.h"


static ImVec2 button_dims = {256, 0};

/*!
 * A main menu GUI scene allowing selection of which scene to proceed to.
 * @implements gui_scene
 */
struct main_menu
{
	struct gui_scene base;
};

static void
scene_render(struct gui_scene *scene, struct gui_program *p)
{
	igBegin("Main Menu", NULL, 0);

	if (igButton("Calibrate", button_dims)) {
		gui_scene_delete_me(p, scene);
		gui_scene_select_video_calibrate(p);
	}

	if (igButton("Debug Test", button_dims)) {
		gui_scene_delete_me(p, scene);

		// If we have created a prober select devices now.
		if (p->xp != NULL) {
			gui_prober_select(p);
		}

		gui_scene_debug(p);
	}

	if (igButton("Remote", button_dims)) {
		gui_scene_delete_me(p, scene);

		gui_scene_remote(p);
	}

	igSeparator();

	if (igButton("Exit", button_dims)) {
		gui_scene_delete_me(p, scene);
	}

	igEnd();
}

static void
scene_destroy(struct gui_scene *scene, struct gui_program *p)
{
	free(scene);
}


/*
 *
 * 'Exported' functions.
 *
 */

void
gui_scene_main_menu(struct gui_program *p)
{
	struct main_menu *mm = U_TYPED_CALLOC(struct main_menu);

	mm->base.render = scene_render;
	mm->base.destroy = scene_destroy;

	gui_scene_push_front(p, &mm->base);
}
