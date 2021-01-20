// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining a XRT graphics provider.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_defines.h"
#include "xrt/xrt_handles.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Pre-declare things, also they should not be in the xrt_iface group.
 *
 */

struct xrt_device;
struct xrt_image_native;

typedef struct VkCommandBuffer_T *VkCommandBuffer;
#ifdef XRT_64_BIT
typedef struct VkImage_T *VkImage;
typedef struct VkDeviceMemory_T *VkDeviceMemory;
#else
typedef uint64_t VkImage;
typedef uint64_t VkDeviceMemory;
#endif


/*!
 * @ingroup xrt_iface
 * @{
 */

/*!
 * Max swapchain images, artificial limit.
 */
#define XRT_MAX_SWAPCHAIN_IMAGES 8

/*!
 * Max formats supported by a compositor, artificial limit.
 */
#define XRT_MAX_SWAPCHAIN_FORMATS 16

/*!
 * Special flags for creating swapchain images.
 */
enum xrt_swapchain_create_flags
{
	//! Our compositor just ignores this bit.
	XRT_SWAPCHAIN_CREATE_PROTECTED_CONTENT = (1 << 0),
	//! Signals that the allocator should only allocate one image.
	XRT_SWAPCHAIN_CREATE_STATIC_IMAGE = (1 << 1),
};

/*!
 * Usage of the swapchain images.
 */
enum xrt_swapchain_usage_bits
{
	XRT_SWAPCHAIN_USAGE_COLOR = 0x00000001,
	XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL = 0x00000002,
	XRT_SWAPCHAIN_USAGE_UNORDERED_ACCESS = 0x00000004,
	XRT_SWAPCHAIN_USAGE_TRANSFER_SRC = 0x00000008,
	XRT_SWAPCHAIN_USAGE_TRANSFER_DST = 0x00000010,
	XRT_SWAPCHAIN_USAGE_SAMPLED = 0x00000020,
	XRT_SWAPCHAIN_USAGE_MUTABLE_FORMAT = 0x00000040,
	XRT_SWAPCHAIN_USAGE_INPUT_ATTACHMENT = 0x00000080,
};

/*!
 * View type to be rendered to by the compositor.
 */
enum xrt_view_type
{
	XRT_VIEW_TYPE_MONO = 1,
	XRT_VIEW_TYPE_STEREO = 2,
};

/*!
 * Layer type.
 */
enum xrt_layer_type
{
	XRT_LAYER_STEREO_PROJECTION,
	XRT_LAYER_STEREO_PROJECTION_DEPTH,
	XRT_LAYER_QUAD,
	XRT_LAYER_CUBE,
	XRT_LAYER_CYLINDER,
	XRT_LAYER_EQUIRECT1,
	XRT_LAYER_EQUIRECT2,
};

/*!
 * Bit field for holding information about how a layer should be composited.
 */
enum xrt_layer_composition_flags
{
	XRT_LAYER_COMPOSITION_CORRECT_CHROMATIC_ABERRATION_BIT = 1 << 0,
	XRT_LAYER_COMPOSITION_BLEND_TEXTURE_SOURCE_ALPHA_BIT = 1 << 1,
	XRT_LAYER_COMPOSITION_UNPREMULTIPLIED_ALPHA_BIT = 1 << 2,
	/*!
	 * The layer is locked to the device and the pose should only be
	 * adjusted for the IPD.
	 */
	XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT = 1 << 3,
};

/*!
 * Which view is the layer visible to?
 *
 * Used for quad layers.
 *
 * @note Doesn't have the same values as the OpenXR counterpart!
 */
enum xrt_layer_eye_visibility
{
	XRT_LAYER_EYE_VISIBILITY_NONE = 0x0,
	XRT_LAYER_EYE_VISIBILITY_LEFT_BIT = 0x1,
	XRT_LAYER_EYE_VISIBILITY_RIGHT_BIT = 0x2,
	XRT_LAYER_EYE_VISIBILITY_BOTH = 0x3,
};

/*!
 * Specifies a sub-image in a layer.
 */
struct xrt_sub_image
{
	//! Image index in the (implicit) swapchain
	uint32_t image_index;
	//! Index in image array (for array textures)
	uint32_t array_index;
	//! The rectangle in the image to use
	struct xrt_rect rect;
};

/*!
 * All of the pure data values associated with a single view in a projection
 * layer.
 *
 * The @ref xrt_swapchain references and @ref xrt_device are provided outside of
 * this struct.
 */
struct xrt_layer_projection_view_data
{
	struct xrt_sub_image sub;

	struct xrt_fov fov;
	struct xrt_pose pose;
};

/*!
 * All the pure data values associated with a stereo projection layer.
 *
 * The @ref xrt_swapchain references and @ref xrt_device are provided outside of
 * this struct.
 */
struct xrt_layer_stereo_projection_data
{
	struct xrt_layer_projection_view_data l, r;
};

/*!
 * All the pure data values associated with a depth information attached
 * to a layer.
 *
 * The @ref xrt_swapchain references and @ref xrt_device are provided outside of
 * this struct.
 */
struct xrt_layer_depth_data
{
	struct xrt_sub_image sub;

	float min_depth;
	float max_depth;
	float near_z;
	float far_z;
};

/*!
 * All the pure data values associated with a stereo projection layer with depth
 * swapchain attached.
 *
 * The @ref xrt_swapchain references and @ref xrt_device are provided outside of
 * this struct.
 */
struct xrt_layer_stereo_projection_depth_data
{
	struct xrt_layer_projection_view_data l, r;

	struct xrt_layer_depth_data l_d, r_d;
};

/*!
 * All the pure data values associated with a quad layer.
 *
 * The @ref xrt_swapchain references and @ref xrt_device are provided outside of
 * this struct.
 */
struct xrt_layer_quad_data
{
	enum xrt_layer_eye_visibility visibility;

	struct xrt_sub_image sub;

	struct xrt_pose pose;
	struct xrt_vec2 size;
};

/*!
 * All the pure data values associated with a cube layer.
 *
 * The @ref xrt_swapchain references and @ref xrt_device are provided outside of
 * this struct.
 */
struct xrt_layer_cube_data
{
	enum xrt_layer_eye_visibility visibility;

	struct xrt_sub_image sub;

	struct xrt_pose pose;
	uint32_t image_array_index;
};

/*!
 * All the pure data values associated with a cylinder layer.
 *
 * The @ref xrt_swapchain references and @ref xrt_device are provided outside of
 * this struct.
 */
struct xrt_layer_cylinder_data
{
	enum xrt_layer_eye_visibility visibility;

	struct xrt_sub_image sub;

	struct xrt_pose pose;
	float radius;
	float central_angle;
	float aspect_ratio;
};

/*!
 * All the pure data values associated with a equirect1 layer.
 *
 * The @ref xrt_swapchain references and @ref xrt_device are provided outside of
 * this struct.
 */
struct xrt_layer_equirect1_data
{
	enum xrt_layer_eye_visibility visibility;

	struct xrt_sub_image sub;

	struct xrt_pose pose;
	float radius;
	struct xrt_vec2 scale;
	struct xrt_vec2 bias;
};

/*!
 * All the pure data values associated with a equirect2 layer.
 *
 * The @ref xrt_swapchain references and @ref xrt_device are provided outside of
 * this struct.
 */
struct xrt_layer_equirect2_data
{
	enum xrt_layer_eye_visibility visibility;

	struct xrt_sub_image sub;

	struct xrt_pose pose;
	float radius;
	float central_horizontal_angle;
	float upper_vertical_angle;
	float lower_vertical_angle;
};

/*!
 * All the pure data values associated with a composition layer.
 *
 * The @ref xrt_swapchain references and @ref xrt_device are provided outside of
 * this struct.
 */
struct xrt_layer_data
{
	/*!
	 * Tag for compositor layer type.
	 */
	enum xrt_layer_type type;

	/*!
	 * Often @ref XRT_INPUT_GENERIC_HEAD_POSE
	 */
	enum xrt_input_name name;

	/*!
	 * "Display no-earlier-than" timestamp for this layer.
	 *
	 * The layer may be displayed after this point, but must never be
	 * displayed before.
	 */
	uint64_t timestamp;

	/*!
	 * Composition flags
	 */
	enum xrt_layer_composition_flags flags;

	/*!
	 * Whether the main compositor should flip the direction of y when
	 * rendering.
	 *
	 * This is actually an input only to the "main" compositor
	 * comp_compositor. It is overwritten by the various client
	 * implementations of the @ref xrt_compositor interface depending on the
	 * conventions of the associated graphics API. Other @ref
	 * xrt_compositor_native implementations that are not the main
	 * compositor just pass this field along unchanged to the "real"
	 * compositor.
	 */
	bool flip_y;

	/*!
	 * Union of data values for the various layer types.
	 *
	 * The initialized member of this union should match the value of
	 * xrt_layer_data::type. It also should be clear because of the layer
	 * function called between xrt_compositor::layer_begin and
	 * xrt_compositor::layer_commit where this data was passed.
	 */
	union {
		struct xrt_layer_stereo_projection_data stereo;
		struct xrt_layer_stereo_projection_depth_data stereo_depth;
		struct xrt_layer_quad_data quad;
		struct xrt_layer_cube_data cube;
		struct xrt_layer_cylinder_data cylinder;
		struct xrt_layer_equirect1_data equirect1;
		struct xrt_layer_equirect2_data equirect2;
	};
};

/*!
 * @interface xrt_swapchain
 *
 * Common swapchain interface/base.
 *
 * Swapchains are owned by the @ref xrt_compositor that they where created from,
 * it's the state trackers job to ensure all swapchains are destroyed before
 * destroying the @ref xrt_compositor.
 */
struct xrt_swapchain
{
	/*!
	 * Number of images.
	 *
	 * The images themselves are on the subclasses.
	 */
	uint32_t num_images;

	/*!
	 * Must have called release_image before calling this function.
	 */
	void (*destroy)(struct xrt_swapchain *xsc);

	/*!
	 * Obtain the index of the next image to use, without blocking on being
	 * able to write to it.
	 *
	 * See xrAcquireSwapchainImage.
	 *
	 * Caller must make sure that no image is acquired before calling
	 * acquire_image.
	 *
	 * @param xsc Self pointer
	 * @param[out] out_index Image index to use next
	 */
	xrt_result_t (*acquire_image)(struct xrt_swapchain *xsc, uint32_t *out_index);

	/*!
	 * See xrWaitSwapchainImage, state tracker needs to track index.
	 */
	xrt_result_t (*wait_image)(struct xrt_swapchain *xsc, uint64_t timeout, uint32_t index);

	/*!
	 * See xrReleaseSwapchainImage, state tracker needs to track index.
	 */
	xrt_result_t (*release_image)(struct xrt_swapchain *xsc, uint32_t index);
};

/*!
 * @copydoc xrt_swapchain::acquire_image
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_swapchain
 */
static inline xrt_result_t
xrt_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *out_index)
{
	return xsc->acquire_image(xsc, out_index);
}

/*!
 * @copydoc xrt_swapchain::wait_image
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_swapchain
 */
static inline xrt_result_t
xrt_swapchain_wait_image(struct xrt_swapchain *xsc, uint64_t timeout, uint32_t index)
{
	return xsc->wait_image(xsc, timeout, index);
}

/*!
 * @copydoc xrt_swapchain::release_image
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_swapchain
 */
static inline xrt_result_t
xrt_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	return xsc->release_image(xsc, index);
}

/*!
 * @copydoc xrt_swapchain::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * xsc_ptr to null if freed.
 *
 * @public @memberof xrt_swapchain
 */
static inline void
xrt_swapchain_destroy(struct xrt_swapchain **xsc_ptr)
{
	struct xrt_swapchain *xsc = *xsc_ptr;
	if (xsc == NULL) {
		return;
	}

	xsc->destroy(xsc);
	*xsc_ptr = NULL;
}

/*!
 * Event type for compositor events, none means no event was returned.
 */
enum xrt_compositor_event_type
{
	XRT_COMPOSITOR_EVENT_NONE = 0,
	XRT_COMPOSITOR_EVENT_STATE_CHANGE = 1,
	XRT_COMPOSITOR_EVENT_OVERLAY_CHANGE = 2,
};

/*!
 * Session state changes event.
 */
struct xrt_compositor_event_state_change
{
	enum xrt_compositor_event_type type;
	bool visible;
	bool focused;
};

/*!
 * Primary session state changes event.
 */
struct xrt_compositor_event_overlay
{
	enum xrt_compositor_event_type type;
	bool primary_focused;
};

/*!
 * Compositor events union.
 */
union xrt_compositor_event {
	enum xrt_compositor_event_type type;
	struct xrt_compositor_event_state_change state;
	struct xrt_compositor_event_state_change overlay;
};

/*!
 * Swapchain creation info.
 */
struct xrt_swapchain_create_info
{
	enum xrt_swapchain_create_flags create;
	enum xrt_swapchain_usage_bits bits;
	int64_t format;
	uint32_t sample_count;
	uint32_t width;
	uint32_t height;
	uint32_t face_count;
	uint32_t array_size;
	uint32_t mip_count;
};

/*!
 * Session information, mostly overlay extension data.
 */
struct xrt_session_info
{
	bool is_overlay;
	uint64_t flags;
	uint32_t z_order;
};

/*!
 * Capabilities and information about the compositor and device together.
 *
 * For client compositors the formats of the native compositor are translated.
 */
struct xrt_compositor_info
{
	//! Number of formats, never changes.
	uint32_t num_formats;

	//! Supported formats, never changes.
	int64_t formats[XRT_MAX_SWAPCHAIN_FORMATS];
};

/*!
 * @interface xrt_compositor
 *
 * Common compositor client interface/base.
 *
 * A compositor is very much analogous to a `XrSession` but without any of the
 * input functionality, and does have the same life time as a `XrSession`.
 */
struct xrt_compositor
{
	/*!
	 * Capabilities and recommended values information.
	 */
	struct xrt_compositor_info info;

	/*!
	 * Create a swapchain with a set of images.
	 */
	xrt_result_t (*create_swapchain)(struct xrt_compositor *xc,
	                                 const struct xrt_swapchain_create_info *info,
	                                 struct xrt_swapchain **out_xsc);

	/*!
	 * Create a swapchain from a set of native images.
	 */
	xrt_result_t (*import_swapchain)(struct xrt_compositor *xc,
	                                 const struct xrt_swapchain_create_info *info,
	                                 struct xrt_image_native *native_images,
	                                 uint32_t num_images,
	                                 struct xrt_swapchain **out_xsc);

	/*!
	 * Poll events from this compositor.
	 *
	 * This function is very much WIP.
	 */
	xrt_result_t (*poll_events)(struct xrt_compositor *xc, union xrt_compositor_event *out_xce);

	/*!
	 * See xrBeginSession.
	 */
	xrt_result_t (*begin_session)(struct xrt_compositor *xc, enum xrt_view_type view_type);

	/*!
	 * See xrEndSession, unlike the OpenXR one the state tracker is
	 * responsible to call discard frame before calling this function. See
	 * discard_frame.
	 */
	xrt_result_t (*end_session)(struct xrt_compositor *xc);

	/*!
	 * See xrWaitFrame.
	 *
	 * The only requirement on the compositor for the @p frame_id
	 * is that it is a positive number.
	 */
	xrt_result_t (*wait_frame)(struct xrt_compositor *xc,
	                           int64_t *out_frame_id,
	                           uint64_t *predicted_display_time,
	                           uint64_t *predicted_display_period);

	/*!
	 * See xrBeginFrame.
	 */
	xrt_result_t (*begin_frame)(struct xrt_compositor *xc, int64_t frame_id);

	/*!
	 * This isn't in the OpenXR API but is explicit in the XRT interfaces.
	 *
	 * Two calls to xrBeginFrame will cause the state tracker to call.
	 *
	 * ```c
	 * xc->begin_frame(xc, frame_id)
	 * xc->discard_frame(xc, frame_id)
	 * xc->begin_frame(xc, frame_id)
	 * ```
	 */
	xrt_result_t (*discard_frame)(struct xrt_compositor *xc, int64_t frame_id);

	/*!
	 * Begins layer submission, this and the other layer_* calls are
	 * equivalent to xrEndFrame, except over multiple calls. It's only after
	 * @p layer_commit that layers will be displayed. From the point of view
	 * of the swapchain the image is used as soon as it's given in a call.
	 */
	xrt_result_t (*layer_begin)(struct xrt_compositor *xc, int64_t frame_id, enum xrt_blend_mode env_blend_mode);

	/*!
	 * Adds a stereo projection layer for submissions.
	 *
	 * @param xc          Self pointer
	 * @param xdev        The device the layer is relative to.
	 * @param l_xsc       Left swapchain.
	 * @param r_xsc       Right swapchain.
	 * @param data        All of the pure data bits.
	 */
	xrt_result_t (*layer_stereo_projection)(struct xrt_compositor *xc,
	                                        struct xrt_device *xdev,
	                                        struct xrt_swapchain *l_xsc,
	                                        struct xrt_swapchain *r_xsc,
	                                        const struct xrt_layer_data *data);

	/*!
	 * Adds a stereo projection layer for submission, has depth information.
	 *
	 * @param xc          Self pointer
	 * @param xdev        The device the layer is relative to.
	 * @param l_xsc       Left swapchain.
	 * @param r_xsc       Right swapchain.
	 * @param l_d_xsc     Left depth swapchain.
	 * @param r_d_xsc     Right depth swapchain.
	 * @param data        All of the pure data bits.
	 */
	xrt_result_t (*layer_stereo_projection_depth)(struct xrt_compositor *xc,
	                                              struct xrt_device *xdev,
	                                              struct xrt_swapchain *l_xsc,
	                                              struct xrt_swapchain *r_xsc,
	                                              struct xrt_swapchain *l_d_xsc,
	                                              struct xrt_swapchain *r_d_xsc,
	                                              const struct xrt_layer_data *data);

	/*!
	 * Adds a quad layer for submission, the center of the quad is specified
	 * by the pose and extends outwards from it.
	 *
	 * @param xc          Self pointer
	 * @param xdev        The device the layer is relative to.
	 * @param xsc         Swapchain.
	 * @param data        All of the pure data bits.
	 */
	xrt_result_t (*layer_quad)(struct xrt_compositor *xc,
	                           struct xrt_device *xdev,
	                           struct xrt_swapchain *xsc,
	                           const struct xrt_layer_data *data);

	/*!
	 * Adds a cube layer for submission.
	 *
	 * @param xc          Self pointer
	 * @param xdev        The device the layer is relative to.
	 * @param xsc         Swapchain.
	 * @param data        All of the pure data bits.
	 */
	xrt_result_t (*layer_cube)(struct xrt_compositor *xc,
	                           struct xrt_device *xdev,
	                           struct xrt_swapchain *xsc,
	                           const struct xrt_layer_data *data);

	/*!
	 * Adds a cylinder layer for submission.
	 *
	 * @param xc          Self pointer
	 * @param xdev        The device the layer is relative to.
	 * @param xsc         Swapchain.
	 * @param data        All of the pure data bits.
	 */
	xrt_result_t (*layer_cylinder)(struct xrt_compositor *xc,
	                               struct xrt_device *xdev,
	                               struct xrt_swapchain *xsc,
	                               const struct xrt_layer_data *data);

	/*!
	 * Adds a equirect1 layer for submission.
	 *
	 * @param xc          Self pointer
	 * @param xdev        The device the layer is relative to.
	 * @param xsc         Swapchain.
	 * @param data        All of the pure data bits.
	 */
	xrt_result_t (*layer_equirect1)(struct xrt_compositor *xc,
	                                struct xrt_device *xdev,
	                                struct xrt_swapchain *xsc,
	                                const struct xrt_layer_data *data);


	/*!
	 * Adds a equirect2 layer for submission.
	 *
	 * @param xc          Self pointer
	 * @param xdev        The device the layer is relative to.
	 * @param xsc         Swapchain.
	 * @param data        All of the pure data bits.
	 */
	xrt_result_t (*layer_equirect2)(struct xrt_compositor *xc,
	                                struct xrt_device *xdev,
	                                struct xrt_swapchain *xsc,
	                                const struct xrt_layer_data *data);

	/*!
	 * Commits all of the submitted layers, it's from this on that the
	 * compositor will use the layers.
	 */
	xrt_result_t (*layer_commit)(struct xrt_compositor *xc,
	                             int64_t frame_id,
	                             xrt_graphics_sync_handle_t sync_handle);

	/*!
	 * Teardown the compositor.
	 *
	 * The state tracker must have made sure that no frames or sessions are
	 * currently pending. See discard_frame, end_frame, end_session.
	 */
	void (*destroy)(struct xrt_compositor *xc);
};

/*!
 * @copydoc xrt_compositor::create_swapchain
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_create_swapchain(struct xrt_compositor *xc,
                          const struct xrt_swapchain_create_info *info,
                          struct xrt_swapchain **out_xsc)
{
	return xc->create_swapchain(xc, info, out_xsc);
}

/*!
 * @copydoc xrt_compositor::import_swapchain
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_import_swapchain(struct xrt_compositor *xc,
                          const struct xrt_swapchain_create_info *info,
                          struct xrt_image_native *native_images,
                          uint32_t num_images,
                          struct xrt_swapchain **out_xsc)
{
	return xc->import_swapchain(xc, info, native_images, num_images, out_xsc);
}

/*!
 * @copydoc xrt_compositor::poll_events
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_poll_events(struct xrt_compositor *xc, union xrt_compositor_event *out_xce)
{
	return xc->poll_events(xc, out_xce);
}

/*!
 * @copydoc xrt_compositor::begin_session
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_begin_session(struct xrt_compositor *xc, enum xrt_view_type view_type)
{
	return xc->begin_session(xc, view_type);
}

/*!
 * @copydoc xrt_compositor::end_session
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_end_session(struct xrt_compositor *xc)
{
	return xc->end_session(xc);
}

/*!
 * @copydoc xrt_compositor::wait_frame
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_wait_frame(struct xrt_compositor *xc,
                    int64_t *out_frame_id,
                    uint64_t *predicted_display_time,
                    uint64_t *predicted_display_period)
{
	return xc->wait_frame(xc, out_frame_id, predicted_display_time, predicted_display_period);
}

/*!
 * @copydoc xrt_compositor::begin_frame
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_begin_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	return xc->begin_frame(xc, frame_id);
}

/*!
 * @copydoc xrt_compositor::discard_frame
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_discard_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	return xc->discard_frame(xc, frame_id);
}

/*!
 * @copydoc xrt_compositor::layer_begin
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_layer_begin(struct xrt_compositor *xc, int64_t frame_id, enum xrt_blend_mode env_blend_mode)
{
	return xc->layer_begin(xc, frame_id, env_blend_mode);
}

/*!
 * @copydoc xrt_compositor::layer_stereo_projection
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_layer_stereo_projection(struct xrt_compositor *xc,
                                 struct xrt_device *xdev,
                                 struct xrt_swapchain *l_xsc,
                                 struct xrt_swapchain *r_xsc,
                                 const struct xrt_layer_data *data)
{
	return xc->layer_stereo_projection(xc, xdev, l_xsc, r_xsc, data);
}

/*!
 * @copydoc xrt_compositor::layer_stereo_projection_depth
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_layer_stereo_projection_depth(struct xrt_compositor *xc,
                                       struct xrt_device *xdev,
                                       struct xrt_swapchain *l_xsc,
                                       struct xrt_swapchain *r_xsc,
                                       struct xrt_swapchain *l_d_xsc,
                                       struct xrt_swapchain *r_d_xsc,
                                       const struct xrt_layer_data *data)
{
	return xc->layer_stereo_projection_depth(xc, xdev, l_xsc, r_xsc, l_d_xsc, r_d_xsc, data);
}

/*!
 * @copydoc xrt_compositor::layer_quad
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_layer_quad(struct xrt_compositor *xc,
                    struct xrt_device *xdev,
                    struct xrt_swapchain *xsc,
                    const struct xrt_layer_data *data)
{
	return xc->layer_quad(xc, xdev, xsc, data);
}

/*!
 * @copydoc xrt_compositor::layer_cube
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_layer_cube(struct xrt_compositor *xc,
                    struct xrt_device *xdev,
                    struct xrt_swapchain *xsc,
                    const struct xrt_layer_data *data)
{
	return xc->layer_cube(xc, xdev, xsc, data);
}

/*!
 * @copydoc xrt_compositor::layer_cylinder
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_layer_cylinder(struct xrt_compositor *xc,
                        struct xrt_device *xdev,
                        struct xrt_swapchain *xsc,
                        const struct xrt_layer_data *data)
{
	return xc->layer_cylinder(xc, xdev, xsc, data);
}


/*!
 * @copydoc xrt_compositor::layer_equirect1
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_layer_equirect1(struct xrt_compositor *xc,
                         struct xrt_device *xdev,
                         struct xrt_swapchain *xsc,
                         const struct xrt_layer_data *data)
{
	return xc->layer_equirect1(xc, xdev, xsc, data);
}

/*!
 * @copydoc xrt_compositor::layer_equirect2
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_layer_equirect2(struct xrt_compositor *xc,
                         struct xrt_device *xdev,
                         struct xrt_swapchain *xsc,
                         const struct xrt_layer_data *data)
{
	return xc->layer_equirect2(xc, xdev, xsc, data);
}

/*!
 * @copydoc xrt_compositor::layer_commit
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_compositor
 */
static inline xrt_result_t
xrt_comp_layer_commit(struct xrt_compositor *xc, int64_t frame_id, xrt_graphics_sync_handle_t sync_handle)
{
	return xc->layer_commit(xc, frame_id, sync_handle);
}

/*!
 * @copydoc xrt_compositor::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * xc_ptr to null if freed.
 *
 * @public @memberof xrt_compositor
 */
static inline void
xrt_comp_destroy(struct xrt_compositor **xc_ptr)
{
	struct xrt_compositor *xc = *xc_ptr;
	if (xc == NULL) {
		return;
	}

	xc->destroy(xc);
	*xc_ptr = NULL;
}


/*
 *
 * OpenGL interface.
 *
 */

/*!
 * Base class for an OpenGL (ES) client swapchain.
 *
 * @ingroup xrt_iface comp_client
 * @extends xrt_swapchain
 */
struct xrt_swapchain_gl
{
	//! @public Base
	struct xrt_swapchain base;

	// GLuint
	unsigned int images[XRT_MAX_SWAPCHAIN_IMAGES];
};

/*!
 * Base class for an OpenGL (ES) client compositor.
 *
 * @ingroup xrt_iface comp_client
 * @extends xrt_compositor
 */
struct xrt_compositor_gl
{
	struct xrt_compositor base;
};

/*!
 * Down-cast helper.
 *
 * @private @memberof xrt_swapchain_gl
 *
 * @todo unused - remove?
 */
static inline struct xrt_swapchain_gl *
xrt_swapchain_gl(struct xrt_swapchain *xsc)
{
	return (struct xrt_swapchain_gl *)xsc;
}

/*!
 * Down-cast helper.
 *
 * @private @memberof xrt_compositor_gl
 *
 * @todo unused - remove?
 */
static inline struct xrt_compositor_gl *
xrt_compositor_gl(struct xrt_compositor *xc)
{
	return (struct xrt_compositor_gl *)xc;
}


/*
 *
 * Vulkan interface.
 *
 */

/*!
 * Base class for a Vulkan client swapchain.
 *
 * @ingroup xrt_iface comp_client
 * @extends xrt_swapchain
 */
struct xrt_swapchain_vk
{
	//! @public Base
	struct xrt_swapchain base;

	//! Images to be used by the caller.
	VkImage images[XRT_MAX_SWAPCHAIN_IMAGES];
};

/*!
 * Base class for a Vulkan client compositor.
 *
 * @ingroup xrt_iface comp_client
 * @extends xrt_compositor
 */
struct xrt_compositor_vk
{
	//! @public Base
	struct xrt_compositor base;
};

/*!
 * Down-cast helper.
 *
 * @private @memberof xrt_swapchain_vk
 *
 * @todo unused - remove?
 */
static inline struct xrt_swapchain_vk *
xrt_swapchain_vk(struct xrt_swapchain *xsc)
{
	return (struct xrt_swapchain_vk *)xsc;
}

/*!
 * Down-cast helper.
 *
 * @private @memberof xrt_compositor_vk
 *
 * @todo unused - remove?
 */
static inline struct xrt_compositor_vk *
xrt_compositor_vk(struct xrt_compositor *xc)
{
	return (struct xrt_compositor_vk *)xc;
}


/*
 *
 * Native interface.
 *
 * These types are supported by underlying native buffers, which are DMABUF file
 * descriptors on Linux.
 *
 */

/*!
 * A single image of a swapchain based on native buffer handles.
 *
 * @ingroup xrt_iface comp
 * @see xrt_swapchain_native
 */
struct xrt_image_native
{
	/*!
	 * Native buffer handle.
	 */
	xrt_graphics_buffer_handle_t handle;
	/*!
	 * @brief Buffer size in memory.
	 *
	 * Optional, set to 0 if unknown at allocation time.
	 * If not zero, used for a max memory requirements check when importing
	 * into Vulkan.
	 */
	size_t size;
};

/*!
 * @interface xrt_swapchain_native
 * Base class for a swapchain that exposes a native buffer handle to be imported
 * into a client API.
 *
 * @ingroup xrt_iface comp
 * @extends xrt_swapchain
 */
struct xrt_swapchain_native
{
	//! @public Base
	struct xrt_swapchain base;

	struct xrt_image_native images[XRT_MAX_SWAPCHAIN_IMAGES];
};

/*!
 * @interface xrt_compositor_native
 *
 * Main compositor server interface.
 *
 * @ingroup xrt_iface comp
 * @extends xrt_compositor
 */
struct xrt_compositor_native
{
	//! @public Base
	struct xrt_compositor base;
};

/*!
 * @brief Create a native swapchain with a set of images.
 *
 * A specialized version of @ref xrt_comp_create_swapchain, for use only on @ref
 * xrt_compositor_native.
 *
 * Helper for calling through the base's function pointer then performing the
 * known-safe downcast.
 *
 * @public @memberof xrt_compositor_native
 */
static inline xrt_result_t
xrt_comp_native_create_swapchain(struct xrt_compositor_native *xcn,
                                 const struct xrt_swapchain_create_info *info,
                                 struct xrt_swapchain_native **out_xscn)
{
	struct xrt_swapchain *xsc = NULL;
	xrt_result_t ret = xrt_comp_create_swapchain(&xcn->base, info, &xsc);
	if (ret == XRT_SUCCESS) {
		*out_xscn = (struct xrt_swapchain_native *)xsc;
	}
	return ret;
}

/*!
 * @copydoc xrt_compositor::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * xcn_ptr to null if freed.
 *
 * @public @memberof xrt_compositor_native
 */
static inline void
xrt_comp_native_destroy(struct xrt_compositor_native **xcn_ptr)
{
	struct xrt_compositor_native *xcn = *xcn_ptr;
	if (xcn == NULL) {
		return;
	}

	xcn->base.destroy(&xcn->base);
	*xcn_ptr = NULL;
}


/*
 *
 * System compositor.
 *
 */

/*!
 * Capabilities and information about the system compositor and device together.
 */
struct xrt_system_compositor_info
{
	struct
	{
		struct
		{
			uint32_t width_pixels;
			uint32_t height_pixels;
			uint32_t sample_count;
		} recommended; //!< Recommended for this view.

		struct
		{
			uint32_t width_pixels;
			uint32_t height_pixels;
			uint32_t sample_count;
		} max; //!< Maximums for this view.
	} views[2];    //!< View configuration information.

	//! Maximum number of layers supported by the compositor, never changes.
	uint32_t max_layers;

	/*!
	 * Blend modes supported by the system (the combination of the
	 * compositor and the HMD capabilities), never changes.
	 */
	enum xrt_blend_mode supported_blend_modes;

	//! The vk device as used by the compositor, never changes.
	uint8_t compositor_vk_deviceUUID[XRT_GPU_UUID_SIZE];

	//! The vk device suggested for Vulkan clients, never changes.
	uint8_t client_vk_deviceUUID[XRT_GPU_UUID_SIZE];
};

/*!
 * The system compositor is a long lived object, it has the same life time as a
 * XrSystemID.
 */
struct xrt_system_compositor
{
	//! Info regarding the system.
	struct xrt_system_compositor_info info;

	/*!
	 * Create a new native compositor.
	 *
	 * This signals that you want to start XR, and as such implicitly brings
	 * up a new session. Does not "call" `xrBeginSession`.
	 *
	 * Some system compositors might only support that one  `xrt_compositor`
	 * is always at a time, will return `XRT_ERROR_MULTI_SESSION_NOT_IMPLEMENTED`
	 * if this is the case.
	 */
	xrt_result_t (*create_native_compositor)(struct xrt_system_compositor *xsc,
	                                         const struct xrt_session_info *xsi,
	                                         struct xrt_compositor_native **out_xcn);

	/*!
	 * Teardown the system compositor.
	 *
	 * The state tracker must make sure that no compositors are alive.
	 */
	void (*destroy)(struct xrt_system_compositor *xsc);
};

/*!
 * @copydoc xrt_system_compositor::create_native_compositor
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_system_compositor
 */
static inline xrt_result_t
xrt_syscomp_create_native_compositor(struct xrt_system_compositor *xsc,
                                     const struct xrt_session_info *xsi,
                                     struct xrt_compositor_native **out_xcn)
{
	return xsc->create_native_compositor(xsc, xsi, out_xcn);
}

/*!
 * @copydoc xrt_system_compositor::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * xcn_ptr to null if freed.
 *
 * @public @memberof xrt_system_compositor
 */
static inline void
xrt_syscomp_destroy(struct xrt_system_compositor **xsc_ptr)
{
	struct xrt_system_compositor *xsc = *xsc_ptr;
	if (xsc == NULL) {
		return;
	}

	xsc->destroy(xsc);
	*xsc_ptr = NULL;
}


/*
 *
 * Image allocator.
 *
 */

/*!
 * Allocator for system native images, in general you do not need to free the
 * images as they will be consumed by importing them to the graphics API.
 *
 * @see xrt_image_native
 */
struct xrt_image_native_allocator
{
	/*!
	 * Allocate a set of images suitable to be used to back a swapchain
	 * with the given create info properties (@p xsci).
	 */
	xrt_result_t (*images_allocate)(struct xrt_image_native_allocator *xina,
	                                const struct xrt_swapchain_create_info *xsci,
	                                size_t num_images,
	                                struct xrt_image_native *out_images);

	/*!
	 * Free the given images.
	 */
	xrt_result_t (*images_free)(struct xrt_image_native_allocator *xina,
	                            size_t num_images,
	                            struct xrt_image_native *images);

	/*!
	 * Destroy the image allocator.
	 */
	void (*destroy)(struct xrt_image_native_allocator *xina);
};

/*!
 * @copydoc xrt_image_native_allocator::xrt_images_allocate
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_image_native_allocate
 */
static inline xrt_result_t
xrt_images_allocate(struct xrt_image_native_allocator *xina,
                    const struct xrt_swapchain_create_info *xsci,
                    size_t num_images,
                    struct xrt_image_native *out_images)
{
	return xina->images_allocate(xina, xsci, num_images, out_images);
}

/*!
 * @copydoc xrt_image_native_allocator::images_free
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof xrt_image_native_allocate
 */
static inline xrt_result_t
xrt_images_free(struct xrt_image_native_allocator *xina, size_t num_images, struct xrt_image_native *images)
{
	return xina->images_free(xina, num_images, images);
}

/*!
 * @copydoc xrt_image_native_allocator::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * xina_ptr to null if freed.
 *
 * @public @memberof xrt_image_native_allocator
 */
static inline void
xrt_images_destroy(struct xrt_image_native_allocator **xina_ptr)
{
	struct xrt_image_native_allocator *xina = *xina_ptr;
	if (xina == NULL) {
		return;
	}

	xina->destroy(xina);
	*xina_ptr = NULL;
}


/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
