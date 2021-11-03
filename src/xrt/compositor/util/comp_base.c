// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helper implementation for native compositors.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_util
 */

#include "util/u_trace_marker.h"

#include "util/comp_base.h"


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

	uint32_t layer_id = cb->slot.num_layers;

	struct comp_layer *layer = &cb->slot.layers[layer_id];
	layer->sc_array[0] = comp_swapchain(xsc);
	layer->sc_array[1] = NULL;
	layer->data = *data;

	cb->slot.num_layers++;

	return XRT_SUCCESS;
}


/*
 *
 * xrt_compositor functions.
 *
 */

static xrt_result_t
base_create_swapchain(struct xrt_compositor *xc,
                      const struct xrt_swapchain_create_info *info,
                      struct xrt_swapchain **out_xsc)
{
	struct comp_base *cb = comp_base(xc);

	return comp_swapchain_create(&cb->vk, &cb->cscgc, info, out_xsc);
}

static xrt_result_t
base_import_swapchain(struct xrt_compositor *xc,
                      const struct xrt_swapchain_create_info *info,
                      struct xrt_image_native *native_images,
                      uint32_t num_images,
                      struct xrt_swapchain **out_xsc)
{
	struct comp_base *cb = comp_base(xc);

	return comp_swapchain_import(&cb->vk, &cb->cscgc, info, native_images, num_images, out_xsc);
}

static xrt_result_t
base_import_fence(struct xrt_compositor *xc, xrt_graphics_sync_handle_t handle, struct xrt_compositor_fence **out_xcf)
{
	struct comp_base *cb = comp_base(xc);

	return comp_fence_import(&cb->vk, handle, out_xcf);
}

static xrt_result_t
base_layer_begin(struct xrt_compositor *xc,
                 int64_t frame_id,
                 uint64_t display_time_ns,
                 enum xrt_blend_mode env_blend_mode)
{
	struct comp_base *cb = comp_base(xc);

	cb->slot.env_blend_mode = env_blend_mode;
	cb->slot.num_layers = 0;

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

	uint32_t layer_id = cb->slot.num_layers;

	struct comp_layer *layer = &cb->slot.layers[layer_id];
	layer->sc_array[0] = comp_swapchain(l_xsc);
	layer->sc_array[1] = comp_swapchain(r_xsc);
	layer->data = *data;

	cb->slot.num_layers++;

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

	uint32_t layer_id = cb->slot.num_layers;

	struct comp_layer *layer = &cb->slot.layers[layer_id];
	layer->sc_array[0] = comp_swapchain(l_xsc);
	layer->sc_array[1] = comp_swapchain(r_xsc);
	layer->sc_array[2] = comp_swapchain(l_d_xsc);
	layer->sc_array[3] = comp_swapchain(r_d_xsc);
	layer->data = *data;

	cb->slot.num_layers++;

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
#if 0
	return do_single_layer(xc, xdev, xsc, data);
#else
	return XRT_SUCCESS; //! @todo Implement
#endif
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

	uint64_t now_ns = os_monotonic_get_ns();
	if (now_ns < wake_up_time_ns) {
		uint32_t delay = (uint32_t)(wake_up_time_ns - now_ns);
		os_precise_sleeper_nanosleep(&cb->sleeper, delay);
	}

	now_ns = os_monotonic_get_ns();

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
	cb->base.base.create_swapchain = base_create_swapchain;
	cb->base.base.import_swapchain = base_import_swapchain;
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

	u_threading_stack_init(&cb->cscgc.destroy_swapchains);

	os_precise_sleeper_init(&cb->sleeper);
}

void
comp_base_fini(struct comp_base *cb)
{
	os_precise_sleeper_deinit(&cb->sleeper);

	u_threading_stack_fini(&cb->cscgc.destroy_swapchains);
}
