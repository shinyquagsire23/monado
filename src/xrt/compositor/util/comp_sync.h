// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Independent @ref xrt_compositor_fence implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_util
 */

#pragma once

#include "xrt/xrt_handles.h"
#include "xrt/xrt_compositor.h"
#include "vk/vk_helpers.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * For importing @ref xrt_graphics_sync_handle_t and turn them into a @ref xrt_compositor_fence.
 *
 * The vk_bundle is owned by the compositor, its the state trackers job to make
 * sure that compositor lives for as long as the fence does and that all fences
 * are destroyed before the compositor is destroyed.
 *
 * @ingroup comp_util
 */
xrt_result_t
comp_fence_import(struct vk_bundle *vk, xrt_graphics_sync_handle_t handle, struct xrt_compositor_fence **out_xcf);


#ifdef __cplusplus
}
#endif
