// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  @ref xrt_frame_sink converters and other helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "os/os_threading.h"
#include "xrt/xrt_frame.h"


#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @see u_sink_quirk_create
 */
struct u_sink_quirk_params
{
	bool stereo_sbs;
	bool ps4_cam;
	bool leap_motion;
};

/*!
 * @public @memberof xrt_frame_sink
 * @see xrt_frame_context
 */
void
u_sink_create_format_converter(struct xrt_frame_context *xfctx,
                               enum xrt_format f,
                               struct xrt_frame_sink *downstream,
                               struct xrt_frame_sink **out_xfs);

/*!
 * @public @memberof xrt_frame_sink
 * @see xrt_frame_context
 */
void
u_sink_create_to_r8g8b8_or_l8(struct xrt_frame_context *xfctx,
                              struct xrt_frame_sink *downstream,
                              struct xrt_frame_sink **out_xfs);

/*!
 * @public @memberof xrt_frame_sink
 * @see xrt_frame_context
 */
void
u_sink_create_to_r8g8b8_bayer_or_l8(struct xrt_frame_context *xfctx,
                                    struct xrt_frame_sink *downstream,
                                    struct xrt_frame_sink **out_xfs);

/*!
 * @public @memberof xrt_frame_sink
 * @see xrt_frame_context
 */
void
u_sink_create_to_rgb_yuv_yuyv_uyvy_or_l8(struct xrt_frame_context *xfctx,
                                         struct xrt_frame_sink *downstream,
                                         struct xrt_frame_sink **out_xfs);

/*!
 * @public @memberof xrt_frame_sink
 * @see xrt_frame_context
 */
void
u_sink_create_to_yuv_yuyv_uyvy_or_l8(struct xrt_frame_context *xfctx,
                                     struct xrt_frame_sink *downstream,
                                     struct xrt_frame_sink **out_xfs);

/*!
 * @public @memberof xrt_frame_sink
 * @see xrt_frame_context
 */
void
u_sink_create_to_yuv_or_yuyv(struct xrt_frame_context *xfctx,
                             struct xrt_frame_sink *downstream,
                             struct xrt_frame_sink **out_xfs);
/*!
 * @public @memberof xrt_frame_sink
 * @see xrt_frame_context
 */
void
u_sink_deinterleaver_create(struct xrt_frame_context *xfctx,
                            struct xrt_frame_sink *downstream,
                            struct xrt_frame_sink **out_xfs);

/*!
 * @public @memberof xrt_frame_sink
 * @see xrt_frame_context
 */
bool
u_sink_queue_create(struct xrt_frame_context *xfctx,
                    struct xrt_frame_sink *downstream,
                    struct xrt_frame_sink **out_xfs);

/*!
 * @public @memberof xrt_frame_sink
 * @see xrt_frame_context
 */
void
u_sink_quirk_create(struct xrt_frame_context *xfctx,
                    struct xrt_frame_sink *downstream,
                    struct u_sink_quirk_params *params,
                    struct xrt_frame_sink **out_xfs);

/*!
 * @public @memberof xrt_frame_sink
 * @see xrt_frame_context
 */
void
u_sink_split_create(struct xrt_frame_context *xfctx,
                    struct xrt_frame_sink *left,
                    struct xrt_frame_sink *right,
                    struct xrt_frame_sink **out_xfs);

/*!
 * Combines stereo frames.
 */
bool
u_sink_combiner_create(struct xrt_frame_context *xfctx,
                       struct xrt_frame_sink *downstream,
                       struct xrt_frame_sink **out_left_xfs,
                       struct xrt_frame_sink **out_right_xfs);


/*
 *
 * Debugging sink,
 *
 */

/*!
 * Allows more safely to debug sink inputs and outputs.
 */
struct u_sink_debug
{
	//! Is initialised/destroyed when added or root is removed.
	struct os_mutex mutex;

	// Protected by mutex, mutex must be held when frame is being pushed.
	struct xrt_frame_sink *sink;
};

static inline void
u_sink_debug_init(struct u_sink_debug *usd)
{
	os_mutex_init(&usd->mutex);
}

static inline bool
u_sink_debug_is_active(struct u_sink_debug *usd)
{
	os_mutex_lock(&usd->mutex);
	bool active = usd->sink != NULL;
	os_mutex_unlock(&usd->mutex);

	return active;
}

static inline void
u_sink_debug_push_frame(struct u_sink_debug *usd, struct xrt_frame *xf)
{
	os_mutex_lock(&usd->mutex);
	if (usd->sink != NULL) {
		xrt_sink_push_frame(usd->sink, xf);
	}
	os_mutex_unlock(&usd->mutex);
}

static inline void
u_sink_debug_set_sink(struct u_sink_debug *usd, struct xrt_frame_sink *xfs)
{
	os_mutex_lock(&usd->mutex);
	usd->sink = xfs;
	os_mutex_unlock(&usd->mutex);
}

static inline void
u_sink_debug_destroy(struct u_sink_debug *usd)
{
	os_mutex_destroy(&usd->mutex);
}


#ifdef __cplusplus
}
#endif
