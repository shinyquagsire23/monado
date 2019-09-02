// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Frameserver interface for video drivers.
 * @author Pete Black <pblack@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_frame.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Controlling the camera capture parameters
 *
 * Used to configure cameras. since there is no guarantee every
 * frameserver will support any/all of these params, a 'best effort'
 * should be made to apply them. all numeric values are normalised
 * floats for broad applicability.
 *
 * @ingroup xrt_iface
 */
struct xrt_fs_capture_parameters
{
	float gain;
	float exposure;
};

struct xrt_fs_mode
{
	uint32_t width;
	uint32_t height;
	enum xrt_format format;
	enum xrt_stereo_format stereo_format;
};

/*!
 * Frameserver that generates frame, multiple subframes (like stereo and
 * mipmaps) can be generate in one frame.
 *
 * @ingroup xrt_iface
 */
struct xrt_fs
{
	/*!
	 * All frames produced by this frameserver is tagged with this id.
	 */
	uint64_t source_id;

	/*!
	 * Enumerate all available modes that this frameserver supports.
	 */
	bool (*enumerate_modes)(struct xrt_fs *xfs,
	                        struct xrt_fs_mode **out_modes,
	                        uint32_t *out_count);

	/*!
	 * Set the capture parameters, may not be supported on all capture
	 * devices.
	 */
	bool (*configure_capture)(struct xrt_fs *xfs,
	                          struct xrt_fs_capture_parameters *cp);

	/*!
	 * Start the capture stream.
	 */
	bool (*stream_start)(struct xrt_fs *xfs,
	                     struct xrt_frame_sink *xs,
	                     uint32_t descriptor_index);

	/*!
	 * Stop the capture stream.
	 */
	bool (*stream_stop)(struct xrt_fs *xfs);

	/*!
	 * Is the capture stream running.
	 */
	bool (*is_running)(struct xrt_fs *xfs);
};


/*
 *
 * Inline functions.
 *
 */

/*!
 * Helper for xrt_fs::enumerate_modes.
 *
 * @ingroup xrt_iface
 */
static inline XRT_MAYBE_UNUSED bool
xrt_fs_enumerate_modes(struct xrt_fs *xfs,
                       struct xrt_fs_mode **out_modes,
                       uint32_t *out_count)
{
	return xfs->enumerate_modes(xfs, out_modes, out_count);
}

/*!
 * Helper for xrt_fs::configure_capture.
 *
 * @ingroup xrt_iface
 */
static inline XRT_MAYBE_UNUSED bool
xrt_fs_configure_capture(struct xrt_fs *xfs,
                         struct xrt_fs_capture_parameters *cp)
{
	return xfs->configure_capture(xfs, cp);
}

/*!
 * Helper for xrt_fs::stream_start.
 *
 * @ingroup xrt_iface
 */
static inline XRT_MAYBE_UNUSED bool
xrt_fs_stream_start(struct xrt_fs *xfs,
                    struct xrt_frame_sink *xs,
                    uint32_t descriptor_index)
{
	return xfs->stream_start(xfs, xs, descriptor_index);
}

/*!
 * Helper for xrt_fs::stream_stop.
 *
 * @ingroup xrt_iface
 */
static inline XRT_MAYBE_UNUSED bool
xrt_fs_stream_stop(struct xrt_fs *xfs)
{
	return xfs->stream_stop(xfs);
}

/*!
 * Helper for xrt_fs::is_running.
 *
 * @ingroup xrt_iface
 */
static inline XRT_MAYBE_UNUSED bool
xrt_fs_is_running(struct xrt_fs *xfs)
{
	return xfs->is_running(xfs);
}


#ifdef __cplusplus
}
#endif
