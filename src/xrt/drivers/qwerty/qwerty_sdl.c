// Copyright 2021, Mateo de Mayo.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Connection between user-generated SDL events and qwerty devices.
 * @author Mateo de Mayo <mateodemayo@gmail.com>
 * @ingroup drv_qwerty
 */

#include "xrt/xrt_device.h"
#include <SDL2/SDL.h>

void
qwerty_process_event(struct xrt_device **xdevs, size_t num_xdevs, SDL_Event event)
{
	if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_w)
		printf("W pressed and xdevs[0] = %s\n", xdevs[0]->str);
}
