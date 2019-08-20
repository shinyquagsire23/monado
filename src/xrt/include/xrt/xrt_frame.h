// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Data frame header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Basic frame data structure - holds a pointer to buffer.
 *
 * @ingroup xrt_iface
 */
struct xrt_frame
{
	uint32_t width;
	uint32_t height;
	size_t stride;
	size_t size;
	uint8_t *data;

	enum xrt_format format;
	enum xrt_stereo_format stereo_format;

	uint64_t timestamp;
	uint64_t source_timestamp;
	uint64_t source_sequence; //!< sequence id
	uint64_t source_id; //!< Which @ref xrt_fs this frame originated from.
};


/*!
 * A object that is sent frames.
 *
 * @ingroup xrt_iface
 */
struct xrt_frame_sink
{
	/*!
	 * Push a frame into the sink.
	 */
	void (*push_frame)(struct xrt_frame_sink *sink,
	                   struct xrt_frame *frame);
};


#ifdef __cplusplus
}
#endif
