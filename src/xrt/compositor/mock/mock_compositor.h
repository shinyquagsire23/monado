// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief A mock native compositor to use when testing client compositors.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#pragma once
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_handles.h"

#ifdef __cplusplus

#include <type_traits>

extern "C" {
#endif

struct mock_compositor_swapchain;
/*!
 * Mock implementation of a native compositor
 * @implements xrt_compositor_native
 */
struct mock_compositor
{
	struct xrt_compositor_native base;

	//! ID for next swapchain
	uint32_t next_id;

	//! Mock users can populate this pointer to use data from hooks
	void *userdata;

	/*!
	 * Optional function pointers you can populate to hook into the behavior of the mock compositor implementation.
	 *
	 * Providing a function pointer will disable any built-in functionality in the mock for most of these fields.
	 * While you can populate these with a lambda, because they're plain function pointers, you can't have any
	 * captures, so use @ref mock_compositor::userdata to read or write any data from the outside world.
	 */
	struct
	{
		/*!
		 * Optional function pointer for mock compositor, called during
		 * @ref xrt_comp_get_swapchain_create_properties
		 */
		xrt_result_t (*get_swapchain_create_properties)(struct mock_compositor *mc,
		                                                const struct xrt_swapchain_create_info *info,
		                                                struct xrt_swapchain_create_properties *xsccp);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_create_swapchain
		 *
		 * Takes the extra parameter of the typed pointer to the in-progress swapchain @p mcsc , which is
		 * allocated and has basic values populated for it, even if this function pointer is set.
		 */
		xrt_result_t (*create_swapchain)(struct mock_compositor *mc,
		                                 struct mock_compositor_swapchain *mcsc,
		                                 const struct xrt_swapchain_create_info *info,
		                                 struct xrt_swapchain **out_xsc);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_import_swapchain
		 *
		 * Takes the extra parameter of the typed pointer to the in-progress swapchain @p mcsc , which is
		 * allocated and has basic values populated for it, even if this function pointer is set. Does **not**
		 * release the native images passed in if this function pointer is set, so you will have to do that
		 * yourself.
		 */
		xrt_result_t (*import_swapchain)(struct mock_compositor *mc,
		                                 struct mock_compositor_swapchain *mcsc,
		                                 const struct xrt_swapchain_create_info *info,
		                                 struct xrt_image_native *native_images,
		                                 uint32_t image_count,
		                                 struct xrt_swapchain **out_xsc);

		// Mocks for the following not yet implemented
#if 0
		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_import_fence
		 */
		xrt_result_t (*import_fence)(struct mock_compositor *mc,
		                             xrt_graphics_sync_handle_t handle,
		                             struct xrt_compositor_fence **out_xcf);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_create_semaphore
		 */
		xrt_result_t (*create_semaphore)(struct mock_compositor *mc,
		                                 xrt_graphics_sync_handle_t *out_handle,
		                                 struct xrt_compositor_semaphore **out_xcsem);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_poll_events
		 */
		xrt_result_t (*poll_events)(struct mock_compositor *mc, union xrt_compositor_event *out_xce);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_begin_session
		 */
		xrt_result_t (*begin_session)(struct mock_compositor *mc, enum xrt_view_type view_type);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_end_session
		 */
		xrt_result_t (*end_session)(struct mock_compositor *mc);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_predict_frame
		 */
		xrt_result_t (*predict_frame)(struct mock_compositor *mc,
		                              int64_t *out_frame_id,
		                              uint64_t *out_wake_time_ns,
		                              uint64_t *out_predicted_gpu_time_ns,
		                              uint64_t *out_predicted_display_time_ns,
		                              uint64_t *out_predicted_display_period_ns);

		/*!
		 *
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_mark_frame
		 */
		xrt_result_t (*mark_frame)(struct mock_compositor *mc,
		                           int64_t frame_id,
		                           enum xrt_compositor_frame_point point,
		                           uint64_t when_ns);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_wait_frame
		 */
		xrt_result_t (*wait_frame)(struct mock_compositor *mc,
		                           int64_t *out_frame_id,
		                           uint64_t *out_predicted_display_time,
		                           uint64_t *out_predicted_display_period);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_begin_frame
		 */
		xrt_result_t (*begin_frame)(struct mock_compositor *mc, int64_t frame_id);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_discard_frame
		 */
		xrt_result_t (*discard_frame)(struct mock_compositor *mc, int64_t frame_id);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_layer_begin
		 */
		xrt_result_t (*layer_begin)(struct mock_compositor *mc,
		                            int64_t frame_id,
		                            uint64_t display_time_ns,
		                            enum xrt_blend_mode env_blend_mode);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_layer_stereo_projection
		 */
		xrt_result_t (*layer_stereo_projection)(struct mock_compositor *mc,
		                                        struct xrt_device *xdev,
		                                        struct xrt_swapchain *l_xsc,
		                                        struct xrt_swapchain *r_xsc,
		                                        const struct xrt_layer_data *data);

		/*!
		 * Optional function pointer for mock compositor, called during @ref
		 * xrt_comp_layer_stereo_projection_depth
		 */
		xrt_result_t (*layer_stereo_projection_depth)(struct mock_compositor *mc,
		                                              struct xrt_device *xdev,
		                                              struct xrt_swapchain *l_xsc,
		                                              struct xrt_swapchain *r_xsc,
		                                              struct xrt_swapchain *l_d_xsc,
		                                              struct xrt_swapchain *r_d_xsc,
		                                              const struct xrt_layer_data *data);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_layer_quad
		 */
		xrt_result_t (*layer_quad)(struct mock_compositor *mc,
		                           struct xrt_device *xdev,
		                           struct xrt_swapchain *xsc,
		                           const struct xrt_layer_data *data);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_layer_cube
		 */
		xrt_result_t (*layer_cube)(struct mock_compositor *mc,
		                           struct xrt_device *xdev,
		                           struct xrt_swapchain *xsc,
		                           const struct xrt_layer_data *data);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_layer_cylinder
		 */
		xrt_result_t (*layer_cylinder)(struct mock_compositor *mc,
		                               struct xrt_device *xdev,
		                               struct xrt_swapchain *xsc,
		                               const struct xrt_layer_data *data);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_layer_equirect1
		 */
		xrt_result_t (*layer_equirect1)(struct mock_compositor *mc,
		                                struct xrt_device *xdev,
		                                struct xrt_swapchain *xsc,
		                                const struct xrt_layer_data *data);


		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_layer_equirect2
		 */
		xrt_result_t (*layer_equirect2)(struct mock_compositor *mc,
		                                struct xrt_device *xdev,
		                                struct xrt_swapchain *xsc,
		                                const struct xrt_layer_data *data);

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_layer_commit
		 */
		xrt_result_t (*layer_commit)(struct mock_compositor *mc,
		                             int64_t frame_id,
		                             xrt_graphics_sync_handle_t sync_handle);

		/*!
		 * Optional function pointer for mock compositor, called during @ref
		 * xrt_comp_layer_commit_with_semaphore
		 */
		xrt_result_t (*layer_commit_with_semaphore)(struct mock_compositor *mc,
		                                            int64_t frame_id,
		                                            struct xrt_compositor_semaphore *xcsem,
		                                            uint64_t value);
#endif

		/*!
		 * Optional function pointer for mock compositor, called during @ref xrt_comp_destroy (before actual
		 * destruction)
		 *
		 * The actual destruction is done by the mock implementation whether or not you populate this field.
		 */
		void (*destroy)(struct mock_compositor *mc);
	} compositor_hooks;

	/*!
	 * Optional function pointers you can populate to hook into the behavior of the mock swapchain implementation.
	 *
	 * Providing a function pointer will disable any built-in functionality in the mock for most of these fields.
	 * While you can populate these with a lambda, because they're plain function pointers, you can't have any
	 * captures, so use @ref mock_compositor::userdata to read or write any data from the outside world.
	 */
	struct
	{
		/*!
		 * Optional function pointer, called during @ref xrt_swapchain::destroy (before actual
		 * destruction)
		 *
		 * The actual destruction is done by the mock implementation whether or not you populate this field.
		 */
		void (*destroy)(struct mock_compositor *mc, struct mock_compositor_swapchain *mcsc);

		/*!
		 * Optional function pointer, called during @ref xrt_swapchain::acquire_image
		 */
		xrt_result_t (*acquire_image)(struct mock_compositor *mc,
		                              struct mock_compositor_swapchain *mcsc,
		                              uint32_t *out_index);

		/*!
		 * Optional function pointer, called during @ref xrt_swapchain::wait_image
		 */
		xrt_result_t (*wait_image)(struct mock_compositor *mc,
		                           struct mock_compositor_swapchain *mcsc,
		                           uint64_t timeout_ns,
		                           uint32_t index);

		/*!
		 * Optional function pointer, called during @ref xrt_swapchain::release_image
		 */
		xrt_result_t (*release_image)(struct mock_compositor *mc,
		                              struct mock_compositor_swapchain *mcsc,
		                              uint32_t index);
	} swapchain_hooks;
};

/*!
 * @brief Cast a generic @ref xrt_compositor pointer (that you know externally is a @ref mock_compositor) to a @p
 * mock_compositor pointer.
 */
static inline struct mock_compositor *
mock_compositor(xrt_compositor *xc)
{
	return (struct mock_compositor *)(xc);
}

/*!
 * Mock implementation of @ref xrt_swapchain_native
 */
struct mock_compositor_swapchain
{
	struct xrt_swapchain_native base;

	//! A swapchain ID, assigned by create_swapchain/import_swapchain
	uint32_t id;

	//! Set if this swapchain was created by import_swapchain
	bool imported;

	//! Populated by copying the create info passed to create_swapchain/import_swapchain
	xrt_swapchain_create_info info;
	/**
	 * Native handles for images.
	 * Populated by the import_swapchain mock if not hooked.
	 * Will be released/unreferenced at destruction by default.
	 */
	xrt_graphics_buffer_handle_t handles[XRT_MAX_SWAPCHAIN_IMAGES];

	//! Modified by the default mock implementations of acquire_image and release_image
	bool acquired[XRT_MAX_SWAPCHAIN_IMAGES];

	//! Modified by the default mock implementations of wait_image and release_image
	bool waited[XRT_MAX_SWAPCHAIN_IMAGES];

	/**
	 * The image ID that will next be acquired.
	 *
	 * The default minimal mock implementation just increments this, modulo image count, regardless of
	 * acquire/wait/release status.
	 */
	uint32_t next_to_acquire;

	//! non-owning pointer to parent
	struct mock_compositor *mc;
};

/*!
 * Cast a generic @ref xrt_swapchain pointer (that you know externally is a @ref mock_compositor_swapchain) to a @p
 * mock_compositor_swapchain pointer.
 */
static inline struct mock_compositor_swapchain *
mock_compositor_swapchain(xrt_swapchain *xsc)
{
	return (struct mock_compositor_swapchain *)(xsc);
}

/*!
 * Create a mock implementation of @ref xrt_compositor_native.
 *
 * The returned value can be passed to @ref mock_compositor() to use the internals of the mock, e.g. to populate
 * hooks to override mock behavior.
 */
struct xrt_compositor_native *
mock_create_native_compositor();

#ifdef __cplusplus

static_assert(std::is_standard_layout<struct mock_compositor>::value);
static_assert(std::is_standard_layout<struct mock_compositor_swapchain>::value);

} // extern "C"
#endif
