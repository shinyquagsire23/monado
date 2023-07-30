// Copyright 2022-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Small helper functions to manage frames.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "main/comp_compositor.h"


/*!
 * Is this frame invalid.
 */
static inline bool
comp_frame_is_invalid_locked(struct comp_frame *f)
{
	return f->id == -1;
}

/*!
 * Clear a slot, need to be externally synchronized.
 */
static inline void
comp_frame_clear_locked(struct comp_frame *slot)
{
	U_ZERO(slot);
	slot->id = -1;
}

/*!
 * Move a frame into a cleared frame, need to be externally synchronized.
 */
static inline void
comp_frame_move_into_cleared(struct comp_frame *dst, struct comp_frame *src)
{
	assert(comp_frame_is_invalid_locked(dst));

	// Copy data.
	*dst = *src;

	U_ZERO(src);
	src->id = -1;
}

/*!
 * Move a frame, clear src, need to be externally synchronized.
 */
static inline void
comp_frame_move_and_clear_locked(struct comp_frame *dst, struct comp_frame *src)
{
	comp_frame_clear_locked(dst);
	comp_frame_move_into_cleared(dst, src);
}
