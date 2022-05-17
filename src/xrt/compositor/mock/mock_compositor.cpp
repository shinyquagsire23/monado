// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief A mock native compositor to use when testing client compositors.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "mock_compositor.h"

#include "util/u_misc.h"
#include "util/u_handles.h"

static void
mock_compositor_swapchain_destroy(struct xrt_swapchain *xsc)
{
	struct mock_compositor_swapchain *mcsc = mock_compositor_swapchain(xsc);
	struct mock_compositor *mc = mcsc->mc;

	if (mc->swapchain_hooks.destroy) {
		mc->swapchain_hooks.destroy(mc, mcsc);
	}
	for (uint32_t i = 0; i < mcsc->base.base.image_count; ++i) {
		u_graphics_buffer_unref(&(mcsc->handles[i]));
	}
	free(xsc);
}

static xrt_result_t
mock_compositor_swapchain_wait_image(struct xrt_swapchain *xsc, uint64_t timeout_ns, uint32_t index)
{
	struct mock_compositor_swapchain *mcsc = mock_compositor_swapchain(xsc);
	struct mock_compositor *mc = mcsc->mc;

	if (mc->swapchain_hooks.wait_image) {
		return mc->swapchain_hooks.wait_image(mc, mcsc, timeout_ns, index);
	}
	mcsc->waited[index] = true;
	return XRT_SUCCESS;
}

static xrt_result_t
mock_compositor_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *out_index)
{
	struct mock_compositor_swapchain *mcsc = mock_compositor_swapchain(xsc);
	struct mock_compositor *mc = mcsc->mc;

	if (mc->swapchain_hooks.acquire_image) {
		return mc->swapchain_hooks.acquire_image(mc, mcsc, out_index);
	}
	uint32_t index = mcsc->next_to_acquire;
	mcsc->next_to_acquire = (mcsc->next_to_acquire + 1) % mcsc->base.base.image_count;
	mcsc->acquired[index] = true;

	return XRT_SUCCESS;
}

static xrt_result_t
mock_compositor_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	struct mock_compositor_swapchain *mcsc = mock_compositor_swapchain(xsc);
	struct mock_compositor *mc = mcsc->mc;

	if (mc->swapchain_hooks.acquire_image) {
		return mc->swapchain_hooks.release_image(mc, mcsc, index);
	}
	mcsc->acquired[index] = false;
	mcsc->waited[index] = false;

	return XRT_SUCCESS;
}


static xrt_result_t
mock_compositor_swapchain_create(struct xrt_compositor *xc,
                                 const struct xrt_swapchain_create_info *info,
                                 struct xrt_swapchain **out_xsc)
{
	struct mock_compositor *mc = mock_compositor(xc);
	// Mini implementation of get_swapchain_create_properties to avoid an actual call causing confusing traces in
	// the mock
	uint32_t image_count = (0 != (info->create & XRT_SWAPCHAIN_CREATE_STATIC_IMAGE)) ? 1 : 3;
	constexpr bool use_dedicated_allocation = false;


	struct mock_compositor_swapchain *mcsc = U_TYPED_CALLOC(struct mock_compositor_swapchain);
	mcsc->base.base.image_count = image_count;
	mcsc->base.base.wait_image = mock_compositor_swapchain_wait_image;
	mcsc->base.base.acquire_image = mock_compositor_swapchain_acquire_image;
	mcsc->base.base.release_image = mock_compositor_swapchain_release_image;
	mcsc->base.base.destroy = mock_compositor_swapchain_destroy;
	mcsc->base.base.reference.count = 1;
	mcsc->mc = mc;
	mcsc->id = mc->next_id;
	mcsc->info = *info;
	mc->next_id++;

	*out_xsc = &mcsc->base.base;
	if (mc->compositor_hooks.create_swapchain) {
		return mc->compositor_hooks.create_swapchain(mc, mcsc, info, out_xsc);
	}

	for (uint32_t i = 0; i < image_count; i++) {
		mcsc->base.images[i].handle = XRT_GRAPHICS_BUFFER_HANDLE_INVALID;
		mcsc->base.images[i].use_dedicated_allocation = use_dedicated_allocation;
	}


	return XRT_SUCCESS;
}

static xrt_result_t
mock_compositor_swapchain_import(struct xrt_compositor *xc,
                                 const struct xrt_swapchain_create_info *info,
                                 struct xrt_image_native *native_images,
                                 uint32_t image_count,
                                 struct xrt_swapchain **out_xsc)
{
	struct mock_compositor *mc = mock_compositor(xc);
	struct mock_compositor_swapchain *mcsc = U_TYPED_CALLOC(struct mock_compositor_swapchain);
	mcsc->base.base.image_count = image_count;
	mcsc->base.base.wait_image = mock_compositor_swapchain_wait_image;
	mcsc->base.base.acquire_image = mock_compositor_swapchain_acquire_image;
	mcsc->base.base.release_image = mock_compositor_swapchain_release_image;
	mcsc->base.base.destroy = mock_compositor_swapchain_destroy;
	mcsc->base.base.reference.count = 1;
	mcsc->imported = true;
	mcsc->mc = mc;
	mcsc->id = mc->next_id;
	mcsc->info = *info;
	mc->next_id++;

	*out_xsc = &mcsc->base.base;
	if (mc->compositor_hooks.import_swapchain) {
		return mc->compositor_hooks.import_swapchain(mc, mcsc, info, native_images, image_count, out_xsc);
	}

	for (uint32_t i = 0; i < image_count; i++) {
		mcsc->handles[i] = native_images[i].handle;
		mcsc->base.images[i] = native_images[i];
	}

	return XRT_SUCCESS;
}

static xrt_result_t
mock_compositor_get_swapchain_create_properties(struct xrt_compositor *xc,
                                                const struct xrt_swapchain_create_info *info,
                                                struct xrt_swapchain_create_properties *xsccp)
{
	struct mock_compositor *mc = mock_compositor(xc);

	if (mc->compositor_hooks.get_swapchain_create_properties) {
		return mc->compositor_hooks.get_swapchain_create_properties(mc, info, xsccp);
	}
	// default "normal" impl
	if (0 != (info->create & XRT_SWAPCHAIN_CREATE_STATIC_IMAGE)) {
		xsccp->image_count = 1;
	} else {
		xsccp->image_count = 3;
	}
	return XRT_SUCCESS;
}

static void
mock_compositor_destroy(struct xrt_compositor *xc)
{
	struct mock_compositor *mc = mock_compositor(xc);

	if (mc->compositor_hooks.destroy) {
		return mc->compositor_hooks.destroy(mc);
	}
	free(mc);
}

struct xrt_compositor_native *
mock_create_native_compositor()
{
	struct mock_compositor *mc = U_TYPED_CALLOC(struct mock_compositor);
	mc->base.base.get_swapchain_create_properties = mock_compositor_get_swapchain_create_properties;
	mc->base.base.create_swapchain = mock_compositor_swapchain_create;
	mc->base.base.import_swapchain = mock_compositor_swapchain_import;
	// mc->base.base.create_semaphore = mock_compositor_semaphore_create;
	// mc->base.base.begin_session = mock_compositor_begin_session;
	// mc->base.base.end_session = mock_compositor_end_session;
	// mc->base.base.wait_frame = mock_compositor_wait_frame;
	// mc->base.base.begin_frame = mock_compositor_begin_frame;
	// mc->base.base.discard_frame = mock_compositor_discard_frame;
	// mc->base.base.layer_begin = mock_compositor_layer_begin;
	// mc->base.base.layer_stereo_projection = mock_compositor_layer_stereo_projection;
	// mc->base.base.layer_stereo_projection_depth = mock_compositor_layer_stereo_projection_depth;
	// mc->base.base.layer_quad = mock_compositor_layer_quad;
	// mc->base.base.layer_cube = mock_compositor_layer_cube;
	// mc->base.base.layer_cylinder = mock_compositor_layer_cylinder;
	// mc->base.base.layer_equirect1 = mock_compositor_layer_equirect1;
	// mc->base.base.layer_equirect2 = mock_compositor_layer_equirect2;
	// mc->base.base.layer_commit = mock_compositor_layer_commit;
	// mc->base.base.layer_commit_with_semaphore = mock_compositor_layer_commit_with_semaphore;
	// mc->base.base.poll_events = mock_compositor_poll_events;
	mc->base.base.destroy = mock_compositor_destroy;

	return &mc->base;
}
