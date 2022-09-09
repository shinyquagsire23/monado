// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining an xrt display or controller device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <mosesturner@protonmail.com>
 * @ingroup xrt_iface
 */

#pragma once

#define XRT_DEVICE_NAME_LEN 256
#define XRT_DEVICE_PRODUCT_NAME_LEN 64 // Incl. termination

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xrt_tracking;


/*!
 * A per-lens/display view information.
 *
 * @ingroup xrt_iface
 */
struct xrt_view
{
	/*!
	 * @brief Viewport position on the screen.
	 *
	 * In absolute screen coordinates on an unrotated display, like the
	 * HMD presents it to the OS.
	 *
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
	 * @brief Physical properties of this display (or the part of a display
	 * that covers this view).
	 *
	 * Not in absolute screen coordinates but like the clients see them i.e.
	 * after rotation is applied by xrt_view::rot.
	 * This field is only used for the clients' swapchain setup.
	 *
	 * The xrt_view::display::w_pixels and xrt_view::display::h_pixels
	 * become the recommended image size for this view, after being scaled
	 * by the debug environment variable `XRT_COMPOSITOR_SCALE_PERCENTAGE`.
	 */
	struct
	{
		uint32_t w_pixels;
		uint32_t h_pixels;
	} display;

	/*!
	 * @brief Rotation 2d matrix used to rotate the position of the output
	 * of the distortion shaders onto the screen.
	 *
	 * If the distortion shader is based on a mesh, then this matrix rotates
	 * the vertex positions.
	 */
	struct xrt_matrix_2x2 rot;
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
	 * @brief The hmd screen as an unrotated display, like the HMD presents
	 * it to the OS.
	 *
	 * This field is used by @ref comp to setup the extended mode window.
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
	 * Array of supported blend modes.
	 */
	enum xrt_blend_mode blend_modes[XRT_MAX_DEVICE_BLEND_MODES];
	size_t blend_mode_count;

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
			//! Data.
			float *vertices;
			//! Number of vertices.
			uint32_t vertex_count;
			//! Stride of vertices
			uint32_t stride;
			//! 1 or 3 for (chromatic aberration).
			uint32_t uv_channels_count;

			//! Indices, for triangle strip.
			int *indices;
			//! Number of indices for the triangle strips (one per view).
			uint32_t index_counts[2];
			//! Offsets for the indices (one offset per view).
			uint32_t index_offsets[2];
			//! Total number of elements in mesh::indices array.
			uint32_t index_count_total;
		} mesh;

		//! distortion is subject to the field of view
		struct xrt_fov fov[2];
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

/*!
 * A single named output, that sits on a @ref xrt_device.
 *
 * @ingroup xrt_iface
 */
struct xrt_output
{
	enum xrt_output_name name;
};


/*!
 * A binding pair, going @p from a binding point to a @p device input.
 *
 * @ingroup xrt_iface
 */
struct xrt_binding_input_pair
{
	enum xrt_input_name from;   //!< From which name.
	enum xrt_input_name device; //!< To input on the device.
};

/*!
 * A binding pair, going @p from a binding point to a @p device output.
 *
 * @ingroup xrt_iface
 */
struct xrt_binding_output_pair
{
	enum xrt_output_name from;   //!< From which name.
	enum xrt_output_name device; //!< To output on the device.
};

/*!
 * A binding profile, has lists of binding pairs to goes from device in @p name
 * to the device it hangs off on.
 *
 * @ingroup xrt_iface
 */
struct xrt_binding_profile
{
	//! Device this binding emulates.
	enum xrt_device_name name;

	struct xrt_binding_input_pair *inputs;
	size_t input_count;
	struct xrt_binding_output_pair *outputs;
	size_t output_count;
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

	//! A unique identifier. Persistent across configurations, if possible.
	char serial[XRT_DEVICE_NAME_LEN];

	//! Null if this device does not interface with the users head.
	struct xrt_hmd_parts *hmd;

	//! Always set, pointing to the tracking system for this device.
	struct xrt_tracking_origin *tracking_origin;

	//! Number of bindings in xrt_device::binding_profiles
	size_t binding_profile_count;
	// Array of alternative binding profiles.
	struct xrt_binding_profile *binding_profiles;

	//! Number of inputs.
	size_t input_count;
	//! Array of input structs.
	struct xrt_input *inputs;

	//! Number of outputs.
	size_t output_count;
	//! Array of output structs.
	struct xrt_output *outputs;

	bool orientation_tracking_supported;
	bool position_tracking_supported;
	bool hand_tracking_supported;
	bool force_feedback_supported;

	/*!
	 * Update any attached inputs.
	 *
	 * @param[in] xdev        The device.
	 */
	void (*update_inputs)(struct xrt_device *xdev);

	/*!
	 * @brief Get relationship of a tracked device to the tracking origin
	 * space as the base space.
	 *
	 * It is the responsibility of the device driver to do any prediction,
	 * there are helper functions available for this.
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
	 * @brief Get relationship of hand joints to the tracking origin space as
	 * the base space.
	 *
	 * It is the responsibility of the device driver to either do prediction
	 * or return joints from a previous time and write that time out to
	 * @p out_timestamp_ns.
	 *
	 * The timestamps are system monotonic timestamps, such as returned by
	 * os_monotonic_get_ns().
	 *
	 * @param[in] xdev                 The device.
	 * @param[in] name                 Some devices may have multiple poses on
	 *                                 them, select the one using this field. For
	 *                                 hand tracking use @p XRT_INPUT_GENERIC_HAND_TRACKING_DEFAULT_SET.
	 * @param[in] desired_timestamp_ns If the device can predict or has a history
	 *                                 of positions, this is when the caller
	 *                                 wants the pose to be from.
	 * @param[out] out_value           The hand joint data read from the device.
	 * @param[out] out_timestamp_ns    The timestamp of the data being returned.
	 *
	 * @see xrt_input_name
	 */
	void (*get_hand_tracking)(struct xrt_device *xdev,
	                          enum xrt_input_name name,
	                          uint64_t desired_timestamp_ns,
	                          struct xrt_hand_joint_set *out_value,
	                          uint64_t *out_timestamp_ns);

	/*!
	 * Set a output value.
	 *
	 * @param[in] xdev           The device.
	 * @param[in] name           The output component name to set.
	 * @param[in] value          The value to set the output to.
	 * @see xrt_output_name
	 */
	void (*set_output)(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value);

	/*!
	 * @brief Get the per-view pose in relation to the view space.
	 *
	 * On most devices with coplanar displays and no built-in eye tracking
	 * or IPD sensing, this just calls a helper to process the provided
	 * eye relation, but this may also handle canted displays as well as
	 * eye tracking.
	 *
	 * Incorporates a call to xrt_device::get_tracked_pose or a wrapper for it
	 *
	 * @param[in] xdev         The device.
	 * @param[in] default_eye_relation
	 *                         The interpupillary relation as a 3D position.
	 *                         Most simple stereo devices would just want to
	 *                         set `out_pose->position.[x|y|z] = ipd.[x|y|z]
	 *                         / 2.0f` and adjust for left vs right view.
	 *                         Not to be confused with IPD that is absolute
	 *                         distance, this is a full 3D translation
	 *                         If a device has a more accurate/dynamic way of
	 *                         knowing the eye relation, it may ignore this
	 *                         input.
	 * @param[in] at_timestamp_ns This is when the caller wants the poses and FOVs to be from.
	 * @param[in] view_count   Number of views.
	 * @param[out] out_head_relation
	 *                         The head pose in the device tracking space.
	 *                         Combine with @p out_poses to get the views in
	 *                         device tracking space.
	 * @param[out] out_fovs    An array (of size @p view_count ) to populate
	 *                         with the device-suggested fields of view.
	 * @param[out] out_poses   An array (of size @p view_count ) to populate
	 *                         with view output poses in head space. When
	 *                         implementing, be sure to also set orientation:
	 *                         most likely identity orientation unless you
	 *                         have canted screens.
	 *                         (Caution: Even if you have eye tracking, you
	 *                         won't use eye orientation here!)
	 */
	void (*get_view_poses)(struct xrt_device *xdev,
	                       const struct xrt_vec3 *default_eye_relation,
	                       uint64_t at_timestamp_ns,
	                       uint32_t view_count,
	                       struct xrt_space_relation *out_head_relation,
	                       struct xrt_fov *out_fovs,
	                       struct xrt_pose *out_poses);
	/**
	 * Compute the distortion at a single point.
	 *
	 * The input is @p u @p v in screen/output space (that is, predistorted), you are to compute and return the u,v
	 * coordinates to sample the render texture. The compositor will step through a range of u,v parameters to build
	 * the lookup (vertex attribute or distortion texture) used to pre-distort the image as required by the device's
	 * optics.
	 *
	 * @param xdev            the device
	 * @param view            the view index
	 * @param u               horizontal texture coordinate
	 * @param v               vertical texture coordinate
	 * @param[out] out_result corresponding u,v pairs for all three color channels.
	 */
	bool (*compute_distortion)(
	    struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *out_result);

	/*!
	 * Destroy device.
	 */
	void (*destroy)(struct xrt_device *xdev);
};

/*!
 * Helper function for @ref xrt_device::update_inputs.
 *
 * @copydoc xrt_device::update_inputs
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
 * @copydoc xrt_device::get_tracked_pose
 *
 * @public @memberof xrt_device
 */
static inline void
xrt_device_get_tracked_pose(struct xrt_device *xdev,
                            enum xrt_input_name name,
                            uint64_t at_timestamp_ns,
                            struct xrt_space_relation *out_relation)
{
	xdev->get_tracked_pose(xdev, name, at_timestamp_ns, out_relation);
}

/*!
 * Helper function for @ref xrt_device::get_hand_tracking.
 *
 * @copydoc xrt_device::get_hand_tracking
 *
 * @public @memberof xrt_device
 */
static inline void
xrt_device_get_hand_tracking(struct xrt_device *xdev,
                             enum xrt_input_name name,
                             uint64_t desired_timestamp_ns,
                             struct xrt_hand_joint_set *out_value,
                             uint64_t *out_timestamp_ns)
{
	xdev->get_hand_tracking(xdev, name, desired_timestamp_ns, out_value, out_timestamp_ns);
}

/*!
 * Helper function for @ref xrt_device::set_output.
 *
 * @copydoc xrt_device::set_output
 *
 * @public @memberof xrt_device
 */
static inline void
xrt_device_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	xdev->set_output(xdev, name, value);
}

/*!
 * Helper function for @ref xrt_device::get_view_poses.
 *
 * @copydoc xrt_device::get_view_poses
 * @public @memberof xrt_device
 */
static inline void
xrt_device_get_view_poses(struct xrt_device *xdev,
                          const struct xrt_vec3 *default_eye_relation,
                          uint64_t at_timestamp_ns,
                          uint32_t view_count,
                          struct xrt_space_relation *out_head_relation,
                          struct xrt_fov *out_fovs,
                          struct xrt_pose *out_poses)
{
	xdev->get_view_poses(xdev, default_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
	                     out_poses);
}

/*!
 * Helper function for @ref xrt_device::compute_distortion.
 *
 * @copydoc xrt_device::compute_distortion
 *
 * @public @memberof xrt_device
 */
static inline void
xrt_device_compute_distortion(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *out_result)
{
	xdev->compute_distortion(xdev, view, u, v, out_result);
}

/*!
 * Helper function for @ref xrt_device::destroy.
 *
 * Handles nulls, sets your pointer to null.
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
} // extern "C"
#endif
