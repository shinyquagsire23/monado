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

struct comp_compositor;

#ifdef __cplusplus
extern "C" {
#endif

enum comp_window_peek_eye
{
	COMP_WINDOW_PEEK_EYE_LEFT = 0,
	COMP_WINDOW_PEEK_EYE_RIGHT = 1,
	COMP_WINDOW_PEEK_EYE_BOTH = 2,
};

struct comp_window_peek;

struct comp_window_peek *
comp_window_peek_create(struct comp_compositor *c);

void
comp_window_peek_destroy(struct comp_window_peek **w_ptr);

void
comp_window_peek_blit(struct comp_window_peek *w, VkImage src, int32_t width, int32_t height);

/*!
 *
 * Getter for the peek window's eye enum.
 * This is a getter function so that struct comp_window_peek can be private.
 *
 * @param[in] w The peek window struct this compositor has.
 * @return The eye that the peek window wants to display.
 *
 */
enum comp_window_peek_eye
comp_window_peek_get_eye(struct comp_window_peek *w);

#ifdef __cplusplus
}
#endif
