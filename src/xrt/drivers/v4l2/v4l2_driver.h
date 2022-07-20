// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  V4L2 frameserver common definitions
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_v4l2
 */


#include "util/u_logging.h"
#include "util/u_sink.h"
#include "xrt/xrt_frameserver.h"
#include <linux/videodev2.h>


/*
 *
 * Defines.
 *
 */

#define V4L2_TRACE(d, ...) U_LOG_IFL_T(d->log_level, __VA_ARGS__)
#define V4L2_DEBUG(d, ...) U_LOG_IFL_D(d->log_level, __VA_ARGS__)
#define V4L2_INFO(d, ...) U_LOG_IFL_I(d->log_level, __VA_ARGS__)
#define V4L2_WARN(d, ...) U_LOG_IFL_W(d->log_level, __VA_ARGS__)
#define V4L2_ERROR(d, ...) U_LOG_IFL_E(d->log_level, __VA_ARGS__)

#define NUM_V4L2_BUFFERS 32


/*
 *
 * Structs
 *
 */

/*!
 * @extends xrt_frame
 * @ingroup drv_v4l2
 */
struct v4l2_frame
{
	struct xrt_frame base;

	void *mem; //!< Data might be at an offset, so we need base memory.

	struct v4l2_buffer v_buf;
};

struct v4l2_state_want
{
	bool active;
	int value;
};

/*!
 * @ingroup drv_v4l2
 */
struct v4l2_control_state
{
	int id;
	int force;

	struct v4l2_state_want want[2];

	int value;

	const char *name;
};

/*!
 * A single open v4l2 capture device, starts its own thread and waits on it.
 *
 * @implements xrt_frame_node
 * @implements xrt_fs
 */
struct v4l2_fs
{
	struct xrt_fs base;

	struct xrt_frame_node node;

	struct u_sink_debug usd;

	int fd;

	struct
	{
		bool extended_format;
		bool timeperframe;
	} has;

	enum xrt_fs_capture_type capture_type;
	struct v4l2_control_state states[256];
	size_t num_states;

	struct
	{
		bool ps4_cam;
	} quirks;

	struct v4l2_frame frames[NUM_V4L2_BUFFERS];
	uint32_t used_frames;

	struct
	{
		bool mmap;
		bool userptr;
	} capture;

	struct xrt_frame_sink *sink;

	pthread_t stream_thread;

	struct v4l2_source_descriptor *descriptors;
	uint32_t num_descriptors;
	uint32_t selected;

	struct xrt_fs_capture_parameters capture_params;

	bool is_configured;
	bool is_running;
	enum u_logging_level log_level;
};

/*!
 * Cast to derived type.
 */
static inline struct v4l2_fs *
v4l2_fs(struct xrt_fs *xfs)
{
	return (struct v4l2_fs *)xfs;
}
