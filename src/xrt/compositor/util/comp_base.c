// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helper implementation for native compositors.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_util
 */

#include "util/u_wait.h"
#include "util/u_trace_marker.h"

#include "util/comp_base.h"
#include "util/comp_semaphore.h"


/*
 *
 * Helper function.
 *
 */

static xrt_result_t
do_single_layer(struct xrt_compositor *xc,
                struct xrt_device *xdev,
                struct xrt_swapchain *xsc,
                const struct xrt_layer_data *data)
{
	struct comp_base *cb = comp_base(xc);

	uint32_t layer_id = cb->slot.layer_count;

	struct comp_layer *layer = &cb->slot.layers[layer_id];
	layer->sc_array[0] = comp_swapchain(xsc);
	layer->sc_array[1] = NULL;
	layer->data = *data;

	cb->slot.layer_count++;

	return XRT_SUCCESS;
}


/*
 *
 * xrt_compositor functions.
 *
 */

static xrt_result_t
base_get_swapchain_create_properties(struct xrt_compositor *xc,
                                     const struct xrt_swapchain_create_info *info,
                                     struct xrt_swapchain_create_properties *xsccp)
{
	return comp_swapchain_get_create_properties(info, xsccp);
}

static xrt_result_t
base_create_swapchain(struct xrt_compositor *xc,
                      const struct xrt_swapchain_create_info *info,
                      struct xrt_swapchain **out_xsc)
{
	struct comp_base *cb = comp_base(xc);

	/*
	 * In case the default get properties function have been overridden
	 * make sure to correctly dispatch the call to get the properties.
	 */
	struct xrt_swapchain_create_properties xsccp = {0};
	xrt_comp_get_swapchain_create_properties(xc, info, &xsccp);

	return comp_swapchain_create(&cb->vk, &cb->cscs, info, &xsccp, out_xsc);
}

static xrt_result_t
base_import_swapchain(struct xrt_compositor *xc,
                      const struct xrt_swapchain_create_info *info,
                      struct xrt_image_native *native_images,
                      uint32_t image_count,
                      struct xrt_swapchain **out_xsc)
{
	struct comp_base *cb = comp_base(xc);

	return comp_swapchain_import(&cb->vk, &cb->cscs, info, native_images, image_count, out_xsc);
}

static xrt_result_t
base_import_fence(struct xrt_compositor *xc, xrt_graphics_sync_handle_t handle, struct xrt_compositor_fence **out_xcf)
{
	struct comp_base *cb = comp_base(xc);

	return comp_fence_import(&cb->vk, handle, out_xcf);
}

static xrt_result_t
base_create_semaphore(struct xrt_compositor *xc,
                      xrt_graphics_sync_handle_t *out_handle,
                      struct xrt_compositor_semaphore **out_xcsem)
{
	struct comp_base *cb = comp_base(xc);

	return comp_semaphore_create(&cb->vk, out_handle, out_xcsem);
}

static xrt_result_t
base_layer_begin(struct xrt_compositor *xc, const struct xrt_layer_frame_data *data)
{
	struct comp_base *cb = comp_base(xc);

	cb->slot.data = *data;
	cb->slot.layer_count = 0;

	return XRT_SUCCESS;
}

static xrt_result_t
base_layer_stereo_projection(struct xrt_compositor *xc,
                             struct xrt_device *xdev,
                             struct xrt_swapchain *l_xsc,
                             struct xrt_swapchain *r_xsc,
                             const struct xrt_layer_data *data)
{
	struct comp_base *cb = comp_base(xc);

	uint32_t layer_id = cb->slot.layer_count;

	struct comp_layer *layer = &cb->slot.layers[layer_id];
	layer->sc_array[0] = comp_swapchain(l_xsc);
	layer->sc_array[1] = comp_swapchain(r_xsc);
	layer->data = *data;

	cb->slot.layer_count++;

	return XRT_SUCCESS;
}

static xrt_result_t
base_layer_stereo_projection_depth(struct xrt_compositor *xc,
                                   struct xrt_device *xdev,
                                   struct xrt_swapchain *l_xsc,
                                   struct xrt_swapchain *r_xsc,
                                   struct xrt_swapchain *l_d_xsc,
                                   struct xrt_swapchain *r_d_xsc,
                                   const struct xrt_layer_data *data)
{
	struct comp_base *cb = comp_base(xc);

	uint32_t layer_id = cb->slot.layer_count;

	struct comp_layer *layer = &cb->slot.layers[layer_id];
	layer->sc_array[0] = comp_swapchain(l_xsc);
	layer->sc_array[1] = comp_swapchain(r_xsc);
	layer->sc_array[2] = comp_swapchain(l_d_xsc);
	layer->sc_array[3] = comp_swapchain(r_d_xsc);
	layer->data = *data;

	cb->slot.layer_count++;

	return XRT_SUCCESS;
}

static xrt_result_t
base_layer_quad(struct xrt_compositor *xc,
                struct xrt_device *xdev,
                struct xrt_swapchain *xsc,
                const struct xrt_layer_data *data)
{
	return do_single_layer(xc, xdev, xsc, data);
}

static xrt_result_t
base_layer_cube(struct xrt_compositor *xc,
                struct xrt_device *xdev,
                struct xrt_swapchain *xsc,
                const struct xrt_layer_data *data)
{
	return do_single_layer(xc, xdev, xsc, data);
}

static xrt_result_t
base_layer_cylinder(struct xrt_compositor *xc,
                    struct xrt_device *xdev,
                    struct xrt_swapchain *xsc,
                    const struct xrt_layer_data *data)
{
	return do_single_layer(xc, xdev, xsc, data);
}

static xrt_result_t
base_layer_equirect1(struct xrt_compositor *xc,
                     struct xrt_device *xdev,
                     struct xrt_swapchain *xsc,
                     const struct xrt_layer_data *data)
{
	return do_single_layer(xc, xdev, xsc, data);
}

static xrt_result_t
base_layer_equirect2(struct xrt_compositor *xc,
                     struct xrt_device *xdev,
                     struct xrt_swapchain *xsc,
                     const struct xrt_layer_data *data)
{
	return do_single_layer(xc, xdev, xsc, data);
}

static xrt_result_t
base_wait_frame(struct xrt_compositor *xc,
                int64_t *out_frame_id,
                uint64_t *out_predicted_display_time_ns,
                uint64_t *out_predicted_display_period_ns)
{
	COMP_TRACE_MARKER();

	struct comp_base *cb = comp_base(xc);

	int64_t frame_id = -1;
	uint64_t wake_up_time_ns = 0;
	uint64_t predicted_gpu_time_ns = 0;

	xrt_comp_predict_frame(               //
	    xc,                               //
	    &frame_id,                        //
	    &wake_up_time_ns,                 //
	    &predicted_gpu_time_ns,           //
	    out_predicted_display_time_ns,    //
	    out_predicted_display_period_ns); //

	// Wait until the given wake up time.
	u_wait_until(&cb->sleeper, wake_up_time_ns);

	uint64_t now_ns = os_monotonic_get_ns();

	// Signal that we woke up.
	xrt_comp_mark_frame(xc, frame_id, XRT_COMPOSITOR_FRAME_POINT_WOKE, now_ns);

	*out_frame_id = frame_id;

	return XRT_SUCCESS;
}


/*
 *
 * 'Exported' functions.
 *
 */

void
comp_base_init(struct comp_base *cb)
{
	cb->base.base.get_swapchain_create_properties = base_get_swapchain_create_properties;
	cb->base.base.create_swapchain = base_create_swapchain;
	cb->base.base.import_swapchain = base_import_swapchain;
	cb->base.base.create_semaphore = base_create_semaphore;
	cb->base.base.import_fence = base_import_fence;
	cb->base.base.layer_begin = base_layer_begin;
	cb->base.base.layer_stereo_projection = base_layer_stereo_projection;
	cb->base.base.layer_stereo_projection_depth = base_layer_stereo_projection_depth;
	cb->base.base.layer_quad = base_layer_quad;
	cb->base.base.layer_cube = base_layer_cube;
	cb->base.base.layer_cylinder = base_layer_cylinder;
	cb->base.base.layer_equirect1 = base_layer_equirect1;
	cb->base.base.layer_equirect2 = base_layer_equirect2;
	cb->base.base.wait_frame = base_wait_frame;

	u_threading_stack_init(&cb->cscs.destroy_swapchains);

	os_precise_sleeper_init(&cb->sleeper);
}

void
comp_base_fini(struct comp_base *cb)
{
	os_precise_sleeper_deinit(&cb->sleeper);

	u_threading_stack_fini(&cb->cscs.destroy_swapchains);
}
