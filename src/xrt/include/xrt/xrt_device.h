// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining an xrt HMD device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#define XRT_DEVICE_NAME_LEN 256

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xrt_tracking;


/*!
 * A per-lens view information.
 *
 * @ingroup xrt_iface
 */
struct xrt_view
{
	/*!
	 * Viewport position on the screen, in absolute screen coordinates.
	 * This field is only used by @ref comp to setup the device rendering.
	 *
	 * If the view is being rotated by xrt_view.rot 90° right in the
	 * distortion shader then `display.w_pixels == viewport.h_pixels` and
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
	 * Pixel and physical properties of this display, not in absolute
	 * screen coordinates that the compositor sees. So before any rotation
	 * is applied by xrt_view::rot.
	 *
	 * The xrt_view::display::w_pixels and xrt_view::display::h_pixels
	 * become the recommended image size for this view.
	 */
	struct
	{
		uint32_t w_pixels;
		uint32_t h_pixels;
		float w_meters;
		float h_meters;
	} display;

	/*!
	 * Position relative to display origin, before any rotation is applied
	 * by xrt_view::rot. note: not set by most drivers, used only for
	 * panotools/ohmd distortion
	 */
	struct
	{
		float x_meters;
		float y_meters;
		int x_pixels;
		int y_pixels;
	} lens_center;

	/*!
	 * Rotation 2d matrix used to rotate the position of the output of the
	 * distortion shaders onto the screen. If the distortion shader is
	 * based on mesh, then this matrix rotates the vertex positions.
	 */
	struct xrt_matrix_2x2 rot;

	/*!
	 * FoV expressed as in OpenXR.
	 */
	struct xrt_fov fov;
};

/*!
 * All of the device components that deals with interfacing to a users head.
 *
 * HMD is probably a bad name for the future but for now will have to do.
 *
 * @ingroup xrt_iface
 */
struct xrt_hmd_parts
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
		} openhmd;

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

		struct
		{
			//! Data.
			float *vertices;
			//! Number of vertices.
			size_t num_vertices;
			//! Stride of vertices
			size_t stride;
			//! 1 or 3 for (chromatic aberration).
			size_t num_uv_channels;

			//! Indices, for triangle strip.
			int *indices;
			//! Number of indices for the triangle strip.
			size_t num_indices[2];
			//! Offsets for the indices.
			size_t offset_indices[2];
			//! Total number of indices.
			size_t total_num_indices;
		} mesh;
	} distortion;
};

/*!
 * A single named input, that sits on a @ref xrt_device.
 *
 * @ingroup xrt_iface
 */
struct xrt_input
{
	//! Is this input active.
	bool active;

	int64_t timestamp;

	enum xrt_input_name name;

	union xrt_input_value value;
};

struct xrt_output
{
	enum xrt_output_name name;
};

/*!
 * @interface xrt_device
 *
 * A single HMD or input device.
 *
 * @ingroup xrt_iface
 */
struct xrt_device
{
	//! Enum identifier of the device.
	enum xrt_device_name name;
	enum xrt_device_type device_type;

	//! A string describing the device.
	char str[XRT_DEVICE_NAME_LEN];

	//! Null if this device does not interface with the users head.
	struct xrt_hmd_parts *hmd;

	//! Always set, pointing to the tracking system for this device.
	struct xrt_tracking_origin *tracking_origin;

	//! Number of inputs.
	size_t num_inputs;
	//! Array of input structs.
	struct xrt_input *inputs;

	//! Number of outputs.
	size_t num_outputs;
	//! Array of output structs.
	struct xrt_output *outputs;

	bool orientation_tracking_supported;
	bool position_tracking_supported;

	/*!
	 * Update any attached inputs.
	 *
	 * @param[in] xdev        The device.
	 */
	void (*update_inputs)(struct xrt_device *xdev);

	/*!
	 * Get relationship of a tracked device to the tracking origin space as
	 * the base space. It is the responsibility of the device driver to do
	 * any prediction, there are helper functions available for this.
	 *
	 * The timestamps are system monotonic timestamps, such as returned by
	 * os_monotonic_get_ns().
	 *
	 * @param[in] xdev           The device.
	 * @param[in] name           Some devices may have multiple poses on
	 *                           them, select the one using this field. For
	 *                           HMDs use @p XRT_INPUT_GENERIC_HEAD_POSE.
	 * @param[in] at_timestamp_ns If the device can predict or has a history
	 *                            of positions, this is when the caller
	 *                            wants the pose to be from.
	 * @param[out] out_relation The relation read from the device.
	 *
	 * @see xrt_input_name
	 */
	void (*get_tracked_pose)(struct xrt_device *xdev,
	                         enum xrt_input_name name,
	                         uint64_t at_timestamp_ns,
	                         struct xrt_space_relation *out_relation);

	/*!
	 * Set a output value.
	 *
	 * @param[in] xdev           The device.
	 * @param[in] name           The output component name to set.
	 * @param[in] value          The value to set the output to.
	 *                           @todo make this param a pointer to const.
	 * @see xrt_output_name
	 */
	void (*set_output)(struct xrt_device *xdev,
	                   enum xrt_output_name name,
	                   union xrt_output_value *value);

	/*!
	 * Get the per view pose in relation to the view space. Does not do any
	 * device level tracking, use get_tracked_pose for that.
	 *
	 * @param[in] xdev         The device.
	 * @param[in] eye_relation The interpupillary relation as a 3D position.
	 *                         Most simple stereo devices would just want to
	 *                         set `out_pose->position.[x|y|z] = ipd.[x|y|z]
	 *                         / 2.0f` and adjust for left vs right view.
	 *                         Not to be confused with IPD that is absolute
	 *                         distance, this is a full 3D translation.
	 *                         @todo make this param a pointer to const.
	 * @param[in] view_index   Index of view.
	 * @param[out] out_pose    Output pose. See eye_relation argument for
	 *                         sample position. Be sure to also set
	 *                         orientation: most likely identity
	 *                         orientation unless you have canted screens.
	 */
	void (*get_view_pose)(struct xrt_device *xdev,
	                      struct xrt_vec3 *eye_relation,
	                      uint32_t view_index,
	                      struct xrt_pose *out_pose);

	bool (*compute_distortion)(struct xrt_device *xdev,
	                           int view,
	                           float u,
	                           float v,
	                           struct xrt_vec2_triplet *result);

	/*!
	 * Destroy device.
	 */
	void (*destroy)(struct xrt_device *xdev);
};

/*!
 * Helper function for @ref xrt_device::update_inputs.
 *
 * @public @memberof xrt_device
 */
static inline void
xrt_device_update_inputs(struct xrt_device *xdev)
{
	xdev->update_inputs(xdev);
}

/*!
 * Helper function for @ref xrt_device::get_tracked_pose.
 *
 * @public @memberof xrt_device
 */
static inline void
xrt_device_get_tracked_pose(struct xrt_device *xdev,
                            enum xrt_input_name name,
                            uint64_t requested_timestamp_ns,
                            struct xrt_space_relation *out_relation)
{
	xdev->get_tracked_pose(xdev, name, requested_timestamp_ns,
	                       out_relation);
}

/*!
 * Helper function for @ref xrt_device::set_output.
 *
 * @public @memberof xrt_device
 */
static inline void
xrt_device_set_output(struct xrt_device *xdev,
                      enum xrt_output_name name,
                      union xrt_output_value *value)
{
	xdev->set_output(xdev, name, value);
}

/*!
 * Helper function for @ref xrt_device::get_view_pose.
 *
 * @public @memberof xrt_device
 */
static inline void
xrt_device_get_view_pose(struct xrt_device *xdev,
                         struct xrt_vec3 *eye_relation,
                         uint32_t view_index,
                         struct xrt_pose *out_pose)
{
	xdev->get_view_pose(xdev, eye_relation, view_index, out_pose);
}

/*!
 * Helper function for @ref xrt_device::destroy.
 *
 * @public @memberof xrt_device
 */
static inline void
xrt_device_destroy(struct xrt_device **xdev_ptr)
{
	struct xrt_device *xdev = *xdev_ptr;
	if (xdev == NULL) {
		return;
	}

	xdev->destroy(xdev);
	*xdev_ptr = NULL;
}


#ifdef __cplusplus
}
#endif
