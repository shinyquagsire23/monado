// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  @ref xrt_frame_sink converters and other helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_frame.h"

#ifdef __cplusplus
extern "C" {
#endif


struct u_sink_quirk_params
{
	bool stereo_sbs;
	bool ps4_cam;
};

void
u_sink_create_format_converter(struct xrt_frame_context *xfctx,
                               enum xrt_format f,
                               struct xrt_frame_sink *downstream,
                               struct xrt_frame_sink **out_xfs);

void
u_sink_create_to_r8g8b8_or_l8(struct xrt_frame_context *xfctx,
                              struct xrt_frame_sink *downstream,
                              struct xrt_frame_sink **out_xfs);

void
u_sink_create_to_yuv_yuyv_or_l8(struct xrt_frame_context *xfctx,
                                struct xrt_frame_sink *downstream,
                                struct xrt_frame_sink **out_xfs);

void
u_sink_create_to_yuv_or_yuyv(struct xrt_frame_context *xfctx,
                             struct xrt_frame_sink *downstream,
                             struct xrt_frame_sink **out_xfs);

void
u_sink_deinterleaver_create(struct xrt_frame_context *xfctx,
                            struct xrt_frame_sink *downstream,
                            struct xrt_frame_sink **out_xfs);

bool
u_sink_queue_create(struct xrt_frame_context *xfctx,
                    struct xrt_frame_sink *downstream,
                    struct xrt_frame_sink **out_xfs);

void
u_sink_quirk_create(struct xrt_frame_context *xfctx,
                    struct xrt_frame_sink *downstream,
                    struct u_sink_quirk_params *params,
                    struct xrt_frame_sink **out_xfs);

void
u_sink_split_create(struct xrt_frame_context *xfctx,
                    struct xrt_frame_sink *left,
                    struct xrt_frame_sink *right,
                    struct xrt_frame_sink **out_xfs);


#ifdef __cplusplus
}
#endif
