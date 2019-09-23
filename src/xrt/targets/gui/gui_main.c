// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main entrypoint for the Monado GUI program.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "util/u_var.h"
#include "gui_common.h"


int
main(int argc, char **argv)
{
	struct program p = {0};
	int ret;

	// Need to do this as early as possible.
	u_var_force_on();

	ret = gui_sdl2_init(&p);
	if (ret != 0) {
		gui_sdl2_quit(&p);
		return ret;
	}

	// To manage the scenes.
	gui_scene_manager_init(&p);

	// Start all of the devices.
	gui_prober_init(&p);

	// First scene to start with.
	if (argc >= 2 && strcmp("debug", argv[1]) == 0) {
		gui_scene_debug(&p);
	} else if (argc >= 2 && strcmp("calibrate", argv[1]) == 0) {
		gui_scene_select_video_calibrate(&p);
	} else {
		gui_scene_main_menu(&p);
	}

	// Main loop.
	gui_imgui_loop(&p);

	// Clean up after us.
	gui_prober_teardown(&p);

	// All scenes should be destroyed by now.
	gui_scene_manager_destroy(&p);

	// Final close.
	gui_sdl2_quit(&p);

	return 0;
}
