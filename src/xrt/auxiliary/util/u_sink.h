// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  @ref xrt_frame_sink converters and other helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "os/os_threading.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_tracking.h"


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
u_sink_create_to_r8g8b8_r8g8b8a8_r8g8b8x8_or_l8(struct xrt_frame_context *xfctx,
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
                    uint64_t max_size,
                    struct xrt_frame_sink *downstream,
                    struct xrt_frame_sink **out_xfs);


/*!
 * @public @memberof xrt_frame_sink
 * @see xrt_frame_context
 */
bool
u_sink_simple_queue_create(struct xrt_frame_context *xfctx,
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
 * Takes a frame and pushes it to two sinks
 */
void
u_sink_split_create(struct xrt_frame_context *xfctx,
                    struct xrt_frame_sink *left,
                    struct xrt_frame_sink *right,
                    struct xrt_frame_sink **out_xfs);

/*!
 * Splits Stereo SBS frames into two independent frames
 */
void
u_sink_stereo_sbs_to_slam_sbs_create(struct xrt_frame_context *xfctx,
                                     struct xrt_frame_sink *downstream_left,
                                     struct xrt_frame_sink *downstream_right,
                                     struct xrt_frame_sink **out_xfs);

/*!
 * Combines stereo frames.
 * Opposite of u_sink_stereo_sbs_to_slam_sbs_create
 */
bool
u_sink_combiner_create(struct xrt_frame_context *xfctx,
                       struct xrt_frame_sink *downstream,
                       struct xrt_frame_sink **out_left_xfs,
                       struct xrt_frame_sink **out_right_xfs);

/*!
 * Enforces left-right push order on frames and forces them to be within a reasonable amount of time from each other
 */
bool
u_sink_force_genlock_create(struct xrt_frame_context *xfctx,
                            struct xrt_frame_sink *downstream_left,
                            struct xrt_frame_sink *downstream_right,
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


/*!
 * @public @memberof xrt_imu_sink
 * @see xrt_frame_context
 * Takes an IMU sample and pushes it to two sinks
 */
void
u_imu_sink_split_create(struct xrt_frame_context *xfctx,
                        struct xrt_imu_sink *downstream_one,
                        struct xrt_imu_sink *downstream_two,
                        struct xrt_imu_sink **out_imu_sink);


/*!
 * @public @memberof xrt_imu_sink
 * @see xrt_frame_context
 * Takes an IMU sample and only pushes it if its timestamp has monotonically increased.
 * Useful for handling hardware inconsistencies.
 */
void
u_imu_sink_force_monotonic_create(struct xrt_frame_context *xfctx,
                                  struct xrt_imu_sink *downstream,
                                  struct xrt_imu_sink **out_imu_sink);


#ifdef __cplusplus
}
#endif
