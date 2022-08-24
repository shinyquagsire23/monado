// Copyright 2022, Simon Zeni <simon@bl4ckb0ne.ca>
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Displays the content of one or both eye onto a desktop window
 * @author Simon Zeni <simon@bl4ckb0ne.ca>
 * @ingroup comp_main
 */

#pragma once

#include "xrt/xrt_config_have.h"

#ifndef XRT_FEATURE_WINDOW_PEEK
#error "XRT_FEATURE_WINDOW_PEEK not enabled"
#endif

#include "os/os_threading.h"

#ifdef XRT_HAVE_SDL2
#include <SDL2/SDL.h>
#else
#error "comp_window_peek.h requires SDL2"
#endif

struct comp_compositor;
struct comp_renderer;

#ifdef __cplusplus
extern "C" {
#endif

enum comp_window_peek_eye
{
	COMP_WINDOW_PEEK_EYE_LEFT = 0,
	COMP_WINDOW_PEEK_EYE_RIGHT = 1,
	COMP_WINDOW_PEEK_EYE_BOTH = 2,
};

struct comp_window_peek
{
	struct comp_target_swapchain base;
	struct comp_compositor *c;

	enum comp_window_peek_eye eye;
	SDL_Window *window;
	uint32_t width, height;
	bool running;
	bool hidden;

	VkSurfaceKHR surface;
	VkSemaphore acquire;
	VkSemaphore submit;
	VkCommandBuffer cmd;

	struct os_thread_helper oth;
};

struct comp_window_peek *
comp_window_peek_create(struct comp_compositor *c);

void
comp_window_peek_destroy(struct comp_window_peek **w_ptr);

void
comp_window_peek_blit(struct comp_window_peek *w, VkImage src, int32_t width, int32_t height);

#ifdef __cplusplus
}
#endif
