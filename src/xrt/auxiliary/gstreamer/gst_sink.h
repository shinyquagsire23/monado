// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  @ref xrt_frame_sink that does gst things.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gstreamer_sink;
struct gstreamer_pipeline;


void
gstreamer_sink_send_eos(struct gstreamer_sink *gs);

uint64_t
gstreamer_sink_get_timestamp_offset(struct gstreamer_sink *gs);

void
gstreamer_sink_create_with_pipeline(struct gstreamer_pipeline *gp,
                                    uint32_t width,
                                    uint32_t height,
                                    enum xrt_format format,
                                    const char *appsrc_name,
                                    struct gstreamer_sink **out_gs,
                                    struct xrt_frame_sink **out_xfs);


#ifdef __cplusplus
}
#endif
