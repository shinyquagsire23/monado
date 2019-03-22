// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining a xrt HMD device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

struct time_state;

/*!
 * A per-lens view information.
 *
 * @ingroup xrt_iface
 */
struct xrt_view
{
	/*!
	 * Viewpport position on the screen, in absolute screen coordinates,
	 * this field is only used by @ref comp to setup the device rendering.
	 *
	 * If the view is being rotated by xrt_view.rot 90° right in the
	 * distortion shader then `display.w_pixels == viewport.h_pixels` &
	 * `display.h_pixels == viewport.w_pixels`.
	 */
	struct
	{
		uint32_t x_pixels;
		uint32_t y_pixels;
		uint32_t w_pixels;
		uint32_t h_pixels;
	} viewport;

	/*!
	 * Pixel and phyisical properties of this display, not in absolute
	 * screen coordinates that the compositor sees. So before any rotation
	 * is applied by xrt_view::rot.
	 *
	 * The xrt_view::display::w_pixels & xrt_view::display::h_pixels
	 * become the recommdnded image size for this view.
	 */
	struct
	{
		uint32_t w_pixels;
		uint32_t h_pixels;
		float w_meters;
		float h_meters;
	} display;

	/*!
	 * Position in meters relative to display origin, before any rotation
	 * is applied by xrt_view::rot.
	 */
	struct
	{
		float x_meters;
		float y_meters;
	} lens_center;

	/*!
	 * Rotation 2d matrix used to rotate the position of the output of the
	 * distortion shaders onto the screen. Should the distortion shader be
	 * based on mesh then this matrix rotates the vertex positions.
	 */
	struct xrt_matrix_2x2 rot;

	/*!
	 * Fov expressed in OpenXR.
	 */
	struct xrt_fov fov;
};

/*!
 * A single HMD device.
 *
 * @ingroup xrt_iface
 */
struct xrt_device
{
	/*!
	 * The hmd screen, right now hardcoded to one.
	 */
	struct
	{
		int w_pixels;
		int h_pixels;
		//! Nominal frame interval
		uint64_t nominal_frame_interval_ns;
	} screens[1];

	/*!
	 * Display information.
	 *
	 * For now hardcoded display to two.
	 */
	struct xrt_view views[2];

	/*!
	 * Supported blend modes, a bitfield.
	 */
	enum xrt_blend_mode blend_mode;

	/*!
	 * Distortion information.
	 */
	struct
	{
		//! Supported distortion models, a bitfield.
		enum xrt_distortion_model models;
		//! Preferred disortion model, single value.
		enum xrt_distortion_model preferred;

		struct
		{
			//! Panotools universal distortion k.
			float distortion_k[4];
			//! Panotools post distortion scale, <r, g, b, _>.
			float aberration_k[4];
			//! Panotools warp scale.
			float warp_scale;
		} pano;

		struct
		{
			float aspect_x_over_y;
			float grow_for_undistort;

			//! Left/right
			float undistort_r2_cutoff[2];

			//! Left/right, x/y
			float center[2][2];

			//! left/right, r/g/b, a/b/c
			float coefficients[2][3][3];
		} vive;

	} distortion;

	/*!
	 * Get relationship of a tracked device to the device "base space".
	 *
	 * Right now the base space is assumed to be local space.
	 *
	 * This is very very WIP and will need to be made a lot more advanced.
	 */
	void (*get_tracked_pose)(struct xrt_device *xdev,
	                         struct time_state *timekeeping,
	                         int64_t *out_timestamp,
	                         struct xrt_space_relation *out_relation);

	/*!
	 * Get the per view pose in relation to the view space. Does not do any
	 * device level tracking, use get_tracked_pose for that.
	 *
	 * @param eye_relation The interpupillary relation as a 3D position,
	 *                     most simple stereo devices would just want to set
	 *                     out_puse->position.[x|y|z] = ipd.[x|y|z] / 2.0f.
	 *                     Not to be confused with IPD that is absolute
	 *                     distance, this is a full 3D relation.
	 * @param index        Index of view.
	 * @param out_pose     Output pose, see ipd argument, and in addition
	 *                     orientation most likely identity rotation.
	 */
	void (*get_view_pose)(struct xrt_device *xdev,
	                      struct xrt_vec3 *eye_relation,
	                      uint32_t view_index,
	                      struct xrt_pose *out_pose);

	/*!
	 * Destroy device.
	 */
	void (*destroy)(struct xrt_device *xdev);
};


#ifdef __cplusplus
}
#endif
