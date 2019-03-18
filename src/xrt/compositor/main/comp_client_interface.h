// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface for client code to compositor.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"

#ifdef __cplusplus
extern "C" {
#endif

struct time_state;

/*!
 * Create the compositor instance using the given device. Used by the client
 * code and implemented by the main compositor code.
 */
struct xrt_compositor_fd*
comp_compositor_create(struct xrt_device* xdev,
                       struct time_state* timekeeping,
                       bool flip_y);


#ifdef __cplusplus
}
#endif
