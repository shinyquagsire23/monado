// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main entrypoint for the Monado GUI program.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup gui
 */

#include "gui_common.h"


int
main(int argc, char **argv)
{
	struct program p = {0};
	int ret;

	ret = gui_sdl2_init(&p);
	if (ret != 0) {
		gui_sdl2_quit(&p);
		return ret;
	}

	gui_sdl2_loop(&p);

	gui_sdl2_quit(&p);

	return 0;
}
