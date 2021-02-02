// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for creating @ref gstreamer_pipeline objects.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_frame.h"

#ifdef __cplusplus
extern "C" {
#endif


struct gstreamer_pipeline;

void
gstreamer_pipeline_create_from_string(struct xrt_frame_context *xfctx,
                                      const char *pipeline_string,
                                      struct gstreamer_pipeline **out_gp);

void
gstreamer_pipeline_create_autovideo_sink(struct xrt_frame_context *xfctx,
                                         const char *appsrc_name,
                                         struct gstreamer_pipeline **out_gp);

void
gstreamer_pipeline_play(struct gstreamer_pipeline *gp);

void
gstreamer_pipeline_stop(struct gstreamer_pipeline *gp);


#ifdef __cplusplus
}
#endif
