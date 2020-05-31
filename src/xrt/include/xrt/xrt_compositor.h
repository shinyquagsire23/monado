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

#ifdef __cplusplus
extern "C" {
#endif


struct xrt_device;

/*!
 * Max swapchain images, artificial limit.
 *
 * @ingroup xrt_iface
 */
#define XRT_MAX_SWAPCHAIN_IMAGES 8

/*!
 * Max formats supported by a compositor, artificial limit.
 *
 * @ingroup xrt_iface
 */
#define XRT_MAX_SWAPCHAIN_FORMATS 8

/*!
 * Special flags for creating swapchain images.
 *
 * @ingroup xrt_iface
 */
enum xrt_swapchain_create_flags
{
	XRT_SWAPCHAIN_CREATE_STATIC_IMAGE = (1 << 0),
};

/*!
 * Usage of the swapchain images.
 *
 * @ingroup xrt_iface
 */
enum xrt_swapchain_usage_bits
{
	XRT_SWAPCHAIN_USAGE_COLOR = 0x00000001,
	XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL = 0x00000002,
	XRT_SWAPCHAIN_USAGE_UNORDERED_ACCESS = 0x00000004,
	XRT_SWAPCHAIN_USAGE_TRANSFER_SRC = 0x00000008,
	XRT_SWAPCHAIN_USAGE_TRANSFER_DST = 0x00000010,
	XRT_SWAPCHAIN_USAGE_SAMPLED = 0x00000020,
	XRT_SWAPCHAIN_USAGE_MUTABLE_FORMAT = 0x00000040
};

/*!
 * View type to be rendered to by the compositor.
 *
 * @ingroup xrt_iface
 */
enum xrt_view_type
{
	XRT_VIEW_TYPE_MONO = 1,
	XRT_VIEW_TYPE_STEREO = 2,
};

/*!
 * Common swapchain base.
 *
 * @ingroup xrt_iface
 */
struct xrt_swapchain
{
	/*!
	 * Number of images, the images themselves are on the subclasses.
	 */
	uint32_t num_images;

	/*!
	 * Must have called release_image before calling this function.
	 */
	void (*destroy)(struct xrt_swapchain *xsc);

	/*!
	 * See xrWaitSwapchainImage, must make sure that no image is acquired
	 * before calling acquire_image.
	 */
	bool (*acquire_image)(struct xrt_swapchain *xsc, uint32_t *index);

	/*!
	 * See xrWaitSwapchainImage, state tracker needs to track index.
	 */
	bool (*wait_image)(struct xrt_swapchain *xsc,
	                   uint64_t timeout,
	                   uint32_t index);

	/*!
	 * See xrReleaseSwapchainImage, state tracker needs to track index.
	 */
	bool (*release_image)(struct xrt_swapchain *xsc, uint32_t index);
};

/*!
 * Helper for xrt_swapchain::acquire_image.
 *
 * @ingroup xrt_iface
 */
static inline bool
xrt_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *index)
{
	return xsc->acquire_image(xsc, index);
}

/*!
 * Helper for xrt_swapchain::wait_image.
 *
 * @ingroup xrt_iface
 */
static inline bool
xrt_swapchain_wait_image(struct xrt_swapchain *xsc,
                         uint64_t timeout,
                         uint32_t index)
{
	return xsc->wait_image(xsc, timeout, index);
}

/*!
 * Helper for xrt_swapchain::release_image.
 *
 * @ingroup xrt_iface
 */
static inline bool
xrt_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	return xsc->release_image(xsc, index);
}

/*!
 * Helper for xrt_swapchain::destroy, does a null check and sets xc_ptr to
 * null if freed.
 *
 * @ingroup xrt_iface
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
 * Common compositor base.
 *
 * @ingroup xrt_iface
 */
struct xrt_compositor
{
	/*!
	 * Number of formats.
	 */
	uint32_t num_formats;

	/*!
	 * Supported formats.
	 */
	int64_t formats[XRT_MAX_SWAPCHAIN_FORMATS];

	/*!
	 * Create a swapchain with a set of images.
	 */
	struct xrt_swapchain *(*create_swapchain)(
	    struct xrt_compositor *xc,
	    enum xrt_swapchain_create_flags create,
	    enum xrt_swapchain_usage_bits bits,
	    int64_t format,
	    uint32_t sample_count,
	    uint32_t width,
	    uint32_t height,
	    uint32_t face_count,
	    uint32_t array_size,
	    uint32_t mip_count);

	/*!
	 * Poll events from this compositor.
	 *
	 * This function is very much WIP.
	 */
	void (*poll_events)(struct xrt_compositor *xc, uint64_t *WIP);

	/*!
	 * This function is implicit in the OpenXR spec but made explicit here.
	 */
	void (*prepare_session)(struct xrt_compositor *xc);

	/*!
	 * See xrBeginSession.
	 */
	void (*begin_session)(struct xrt_compositor *xc,
	                      enum xrt_view_type view_type);

	/*!
	 * See xrEndSession, unlike the OpenXR one the state tracker is
	 * responsible to call discard frame before calling this function. See
	 * discard_frame.
	 */
	void (*end_session)(struct xrt_compositor *xc);

	/*!
	 * See xrWaitFrame.
	 */
	void (*wait_frame)(struct xrt_compositor *xc,
	                   uint64_t *predicted_display_time,
	                   uint64_t *predicted_display_period);

	/*!
	 * See xrBeginFrame.
	 */
	void (*begin_frame)(struct xrt_compositor *xc);

	/*!
	 * This isn't in the OpenXR API but is explicit in the XRT interfaces.
	 *
	 * Two calls to xrBeginFrame will cause the state tracker to call.
	 *
	 * ```c
	 * xc->begin_frame(xc)
	 * xc->discard_frame(xc)
	 * xc->begin_frame(xc)
	 * ```
	 */
	void (*discard_frame)(struct xrt_compositor *xc);

	/*!
	 * Begins layer submission, this and the other layer_* calls equivalent
	 * to xrEndFrame, except over multiple class. It's only after
	 * @p layer_commit that layers will be displayed. From the point of view
	 * of the swapchain the image is used as soon as it's given in a call.
	 */
	void (*layer_begin)(struct xrt_compositor *xc,
	                    enum xrt_blend_mode env_blend_mode);

	/*!
	 * Adds a stereo projection layer for submissions.
	 *
	 * @param timestamp     When should this layer be shown.
	 * @param xdev          The device the layer is relative to.
	 * @param name          Which pose this layer is relative to.
	 * @param layer_flags   Flags for this layer, applied to both images.
	 * @param l_sc          Left swapchain.
	 * @param l_image_index Left image index as return by acquire_image.
	 * @param l_rect        Left subimage rect.
	 * @param l_array_index Left array index.
	 * @param l_fov         Left fov the left projection rendered with.
	 * @param l_pose        Left pose the left projection rendered with.
	 * @param r_sc          Right swapchain.
	 * @param r_image_index Right image index as return by acquire_image.
	 * @param r_rect        Right subimage rect.
	 * @param r_array_index Right array index.
	 * @param r_fov         Right fov the left projection rendered with.
	 * @param r_pose        Right pose the left projection rendered with.
	 * @param flip_y        Flip Y texture coordinates.
	 */
	void (*layer_stereo_projection)(
	    struct xrt_compositor *xc,
	    uint64_t timestamp,
	    struct xrt_device *xdev,
	    enum xrt_input_name name,
	    enum xrt_layer_composition_flags layer_flags,
	    struct xrt_swapchain *l_sc,
	    uint32_t l_image_index,
	    struct xrt_rect *l_rect,
	    uint32_t l_array_index,
	    struct xrt_fov *l_fov,
	    struct xrt_pose *l_pose,
	    struct xrt_swapchain *r_sc,
	    uint32_t r_image_index,
	    struct xrt_rect *r_rect,
	    uint32_t r_array_index,
	    struct xrt_fov *r_fov,
	    struct xrt_pose *r_pose,
	    bool flip_y);

	/*!
	 * Adds a quad layer for submission, the center of the quad is specified
	 * by the pose and extends outwards from it.
	 *
	 * @param timestamp   When should this layer be shown.
	 * @param xdev        The device the layer is relative to.
	 * @param name        Which pose this layer is relative to.
	 * @param layer_flags Flags for this layer.
	 * @param visibility  Which views are is this layer visible in.
	 * @param sc          Swapchain.
	 * @param image_index Image index as return by acquire_image.
	 * @param rect        Subimage rect.
	 * @param array_index Array index.
	 * @param pose        Pose the left projection rendered with.
	 * @param size        Size of the quad in meters.
	 * @param flip_y      Flip Y texture coordinates.
	 */
	void (*layer_quad)(struct xrt_compositor *xc,
	                   uint64_t timestamp,
	                   struct xrt_device *xdev,
	                   enum xrt_input_name name,
	                   enum xrt_layer_composition_flags layer_flags,
	                   enum xrt_layer_eye_visibility visibility,
	                   struct xrt_swapchain *sc,
	                   uint32_t image_index,
	                   struct xrt_rect *rect,
	                   uint32_t array_index,
	                   struct xrt_pose *pose,
	                   struct xrt_vec2 *size,
	                   bool flip_y);

	/*!
	 * Commits all of the submitted layers, it's from this on that the
	 * compositor will use the layers.
	 */
	void (*layer_commit)(struct xrt_compositor *xc);

	/*!
	 * Teardown the compositor.
	 *
	 * The state tracker must have made sure that no frames or sessions are
	 * currently pending. See discard_frame, end_frame, end_session.
	 */
	void (*destroy)(struct xrt_compositor *xc);
};

/*!
 * Helper for xrt_compositor::create_swapchain
 *
 * @ingroup xrt_iface
 */
static inline struct xrt_swapchain *
xrt_comp_create_swapchain(struct xrt_compositor *xc,
                          enum xrt_swapchain_create_flags create,
                          enum xrt_swapchain_usage_bits bits,
                          int64_t format,
                          uint32_t sample_count,
                          uint32_t width,
                          uint32_t height,
                          uint32_t face_count,
                          uint32_t array_size,
                          uint32_t mip_count)
{
	return xc->create_swapchain(xc, create, bits, format, sample_count,
	                            width, height, face_count, array_size,
	                            mip_count);
}

/*!
 * Helper for xrt_compositor::poll_events
 *
 * @ingroup xrt_iface
 */
static inline void
xrt_comp_poll_events(struct xrt_compositor *xc, uint64_t *WIP)
{
	xc->poll_events(xc, WIP);
}

/*!
 * Helper for xrt_compositor::prepare_session
 *
 * @ingroup xrt_iface
 */
static inline void
xrt_comp_prepare_session(struct xrt_compositor *xc)
{
	xc->prepare_session(xc);
}

/*!
 * Helper for xrt_compositor::begin_session
 *
 * @ingroup xrt_iface
 */
static inline void
xrt_comp_begin_session(struct xrt_compositor *xc, enum xrt_view_type view_type)
{
	xc->begin_session(xc, view_type);
}

/*!
 * Helper for xrt_compositor::end_session
 *
 * @ingroup xrt_iface
 */
static inline void
xrt_comp_end_session(struct xrt_compositor *xc)
{
	xc->end_session(xc);
}

/*!
 * Helper for xrt_compositor::wait_frame
 *
 * @ingroup xrt_iface
 */
static inline void
xrt_comp_wait_frame(struct xrt_compositor *xc,
                    uint64_t *predicted_display_time,
                    uint64_t *predicted_display_period)
{
	xc->wait_frame(xc, predicted_display_time, predicted_display_period);
}

/*!
 * Helper for xrt_compositor::begin_frame
 *
 * @ingroup xrt_iface
 */
static inline void
xrt_comp_begin_frame(struct xrt_compositor *xc)
{
	xc->begin_frame(xc);
}

/*!
 * Helper for xrt_compositor::discard_frame
 *
 * @ingroup xrt_iface
 */
static inline void
xrt_comp_discard_frame(struct xrt_compositor *xc)
{
	xc->discard_frame(xc);
}

/*!
 * Helper for xrt_compositor::layer_begin
 *
 * @ingroup xrt_iface
 */
static inline void
xrt_comp_layer_begin(struct xrt_compositor *xc,
                     enum xrt_blend_mode env_blend_mode)
{
	xc->layer_begin(xc, env_blend_mode);
}

/*!
 * Helper for xrt_compositor::layer_stereo_projection
 *
 * @ingroup xrt_iface
 */
static inline void
xrt_comp_layer_stereo_projection(struct xrt_compositor *xc,
                                 uint64_t timestamp,
                                 struct xrt_device *xdev,
                                 enum xrt_input_name name,
                                 enum xrt_layer_composition_flags layer_flags,
                                 struct xrt_swapchain *l_sc,
                                 uint32_t l_image_index,
                                 struct xrt_rect *l_rect,
                                 uint32_t l_array_index,
                                 struct xrt_fov *l_fov,
                                 struct xrt_pose *l_pose,
                                 struct xrt_swapchain *r_sc,
                                 uint32_t r_image_index,
                                 struct xrt_rect *r_rect,
                                 uint32_t r_array_index,
                                 struct xrt_fov *r_fov,
                                 struct xrt_pose *r_pose,
                                 bool flip_y)
{
	xc->layer_stereo_projection(xc, timestamp, xdev, name, layer_flags,
	                            l_sc, l_image_index, l_rect, l_array_index,
	                            l_fov, l_pose, r_sc, r_image_index, r_rect,
	                            r_array_index, r_fov, r_pose, flip_y);
}

/*!
 * Helper for xrt_compositor::layer_quad
 *
 * @ingroup xrt_iface
 */
static inline void
xrt_comp_layer_quad(struct xrt_compositor *xc,
                    uint64_t timestamp,
                    struct xrt_device *xdev,
                    enum xrt_input_name name,
                    enum xrt_layer_composition_flags layer_flags,
                    enum xrt_layer_eye_visibility visibility,
                    struct xrt_swapchain *sc,
                    uint32_t image_index,
                    struct xrt_rect *rect,
                    uint32_t array_index,
                    struct xrt_pose *pose,
                    struct xrt_vec2 *size,
                    bool flip_y)
{
	xc->layer_quad(xc, timestamp, xdev, name, layer_flags, visibility, sc,
	               image_index, rect, array_index, pose, size, flip_y);
}

/*!
 * Helper for xrt_compositor::layer_commit
 *
 * @ingroup xrt_iface
 */
static inline void
xrt_comp_layer_commit(struct xrt_compositor *xc)
{
	xc->layer_commit(xc);
}

/*!
 * Helper for xrt_compositor::destroy, does a null check and sets xc_ptr to
 * null if freed.
 *
 * @ingroup xrt_iface
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
 * @ingroup xrt_iface comp_client
 */
struct xrt_swapchain_gl
{
	struct xrt_swapchain base;

	// GLuint
	unsigned int images[XRT_MAX_SWAPCHAIN_IMAGES];
	// GLuint
	unsigned int memory[XRT_MAX_SWAPCHAIN_IMAGES];
};

/*!
 * @ingroup xrt_iface comp_client
 */
struct xrt_compositor_gl
{
	struct xrt_compositor base;
};

static inline struct xrt_swapchain_gl *
xrt_swapchain_gl(struct xrt_swapchain *xsc)
{
	return (struct xrt_swapchain_gl *)xsc;
}

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

#ifdef XRT_64_BIT
typedef struct VkImage_T *VkImage;
typedef struct VkDeviceMemory_T *VkDeviceMemory;
#else
typedef uint64_t VkImage;
typedef uint64_t VkDeviceMemory;
#endif

/*!
 * Base class for a Vulkan client swapchain.
 *
 * @ingroup xrt_iface comp_client
 */
struct xrt_swapchain_vk
{
	struct xrt_swapchain base;

	VkImage images[XRT_MAX_SWAPCHAIN_IMAGES];
	VkDeviceMemory mems[XRT_MAX_SWAPCHAIN_IMAGES];
};

/*!
 * Base class for a Vulkan client compositor.
 *
 * @ingroup xrt_iface comp_client
 */
struct xrt_compositor_vk
{
	struct xrt_compositor base;
};

static inline struct xrt_swapchain_vk *
xrt_swapchain_vk(struct xrt_swapchain *xsc)
{
	return (struct xrt_swapchain_vk *)xsc;
}

static inline struct xrt_compositor_vk *
xrt_compositor_vk(struct xrt_compositor *xc)
{
	return (struct xrt_compositor_vk *)xc;
}


/*
 *
 * FD interface, aka DMABUF.
 *
 */

/*!
 * A single image of a fd based swapchain.
 *
 * @ingroup xrt_iface comp
 */
struct xrt_image_fd
{
	size_t size;
	int fd;
	int _pad;
};

/*!
 * A swapchain that exposes fd to be imported into a client API.
 *
 * @ingroup xrt_iface comp
 */
struct xrt_swapchain_fd
{
	struct xrt_swapchain base;

	struct xrt_image_fd images[XRT_MAX_SWAPCHAIN_IMAGES];
};

/*!
 * Main compositor.
 *
 * @ingroup xrt_iface comp
 */
struct xrt_compositor_fd
{
	struct xrt_compositor base;
};

static inline struct xrt_swapchain_fd *
xrt_swapchain_fd(struct xrt_swapchain *xsc)
{
	return (struct xrt_swapchain_fd *)xsc;
}

static inline struct xrt_compositor_fd *
xrt_compositor_fd(struct xrt_compositor *xc)
{
	return (struct xrt_compositor_fd *)xc;
}


#ifdef __cplusplus
}
#endif
