// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main file for sdl compositor experiments.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "util/u_trace_marker.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>


// Insert the on load constructor to init trace marker.
U_TRACE_TARGET_SETUP(U_TRACE_WHICH_SERVICE)

int
ipc_server_main(int argc, char *argv[]);


int
main(int argc, char *argv[])
{
	u_trace_marker_init();

	return ipc_server_main(argc, argv);
}
