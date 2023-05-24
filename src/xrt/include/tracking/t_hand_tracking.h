// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Hand tracking interfaces.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_tracking.h"


#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief Image boundary type.
 *
 * Currently used by hand-tracking to determine if parts of the hand are not visible to the camera, ie. they are outside
 * of the camera's vignette.
 *
 * Feel free to move this out of t_hand_tracking if this becomes more generally applicable.
 *
 * @ingroup xrt_iface
 */
enum t_image_boundary_type
{
	HT_IMAGE_BOUNDARY_NONE,
	HT_IMAGE_BOUNDARY_CIRCLE,
};

/*!
 * @brief Circular image boundary.
 *
 * Currently used by hand-tracking to determine if parts of the hand are not visible to the camera, ie. they are outside
 * of the camera's vignette.
 *
 * Feel free to move this out of t_hand_tracking if this becomes more generally applicable.
 *
 * @ingroup xrt_iface
 */
struct t_image_boundary_circle
{
	// The center, in normalized 0-1 UV coordinates.
	// Should probably be between 0 and 1 in pixel coordinates.
	struct xrt_vec2 normalized_center;
	// The radius, divided by the image width.
	// For Index, should be around 0.5.
	float normalized_radius;
};

/*!
 * @brief Logical orientation of the camera image, relative to the user's head.
 * For example, Rift S uses CAMERA_ORIENTATION_90 for the two front cameras.
 *
 * Feel free to move this out of t_hand_tracking if this becomes more generally applicable.
 *
 */
enum t_camera_orientation
{
	CAMERA_ORIENTATION_0 = 0,     // Normal "horizontal" orientation
	CAMERA_ORIENTATION_90 = 90,   // Camera rotated 90° to the right
	CAMERA_ORIENTATION_180 = 180, // Camera rotated 180° upside down
	CAMERA_ORIENTATION_270 = 270, // Camera rotated 270° to the left
};


/*!
 * @brief Information about image boundary and camera orientation for one view.
 *
 * Currently used by hand-tracking to determine if parts of the hand are not
 * visible to the camera, ie. they are outside of the camera's vignette.
 *
 * @ingroup xrt_iface
 */
struct t_camera_extra_info_one_view
{
	enum t_image_boundary_type boundary_type;

	union {
		struct t_image_boundary_circle circle;
	} boundary;

	enum t_camera_orientation camera_orientation;
};

/*!
 * @brief Information about image boundaries and camera orientations for all the
 * cameras used in a tracking system.
 *
 * Currently used by hand-tracking to determine if parts of the hand are not
 * visible to the camera, ie. they are outside of the camera's vignette.
 *
 * @ingroup xrt_iface
 */
struct t_camera_extra_info
{
	//!@todo Hardcoded to 2 - needs to increase as we support headsets with more cameras.
	struct t_camera_extra_info_one_view views[2];
};

/*!
 * Synchronously processes frames and returns two hands.
 */
struct t_hand_tracking_sync
{
	/*!
	 * Process left and right view and get back a result synchronously.
	 */
	void (*process)(struct t_hand_tracking_sync *ht_sync,
	                struct xrt_frame *left_frame,
	                struct xrt_frame *right_frame,
	                struct xrt_hand_joint_set *out_left_hand,
	                struct xrt_hand_joint_set *out_right_hand,
	                uint64_t *out_timestamp_ns);

	/*!
	 * Destroy this hand tracker sync object.
	 */
	void (*destroy)(struct t_hand_tracking_sync *ht_sync);
};

/*!
 * @copydoc t_hand_tracking_sync::process
 *
 * @public @memberof t_hand_tracking_sync
 */
static inline void
t_ht_sync_process(struct t_hand_tracking_sync *ht_sync,
                  struct xrt_frame *left_frame,
                  struct xrt_frame *right_frame,
                  struct xrt_hand_joint_set *out_left_hand,
                  struct xrt_hand_joint_set *out_right_hand,
                  uint64_t *out_timestamp_ns)
{
	ht_sync->process(ht_sync, left_frame, right_frame, out_left_hand, out_right_hand, out_timestamp_ns);
}

/*!
 * @copydoc t_hand_tracking_sync::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * ht_sync_ptr to null if freed.
 *
 * @public @memberof t_hand_tracking_sync
 */
static inline void
t_ht_sync_destroy(struct t_hand_tracking_sync **ht_sync_ptr)
{
	struct t_hand_tracking_sync *ht_sync = *ht_sync_ptr;
	if (ht_sync == NULL) {
		return;
	}

	ht_sync->destroy(ht_sync);
	*ht_sync_ptr = NULL;
}

struct t_hand_tracking_async
{
	struct xrt_frame_node node;
	struct xrt_frame_sink left;
	struct xrt_frame_sink right;
	struct xrt_slam_sinks sinks; //!< Pointers to `left` and `right` sinks

	void (*get_hand)(struct t_hand_tracking_async *ht_async,
	                 enum xrt_input_name name,
	                 uint64_t desired_timestamp_ns,
	                 struct xrt_hand_joint_set *out_value,
	                 uint64_t *out_timestamp_ns);
};

struct t_hand_tracking_async *
t_hand_tracking_async_default_create(struct xrt_frame_context *xfctx, struct t_hand_tracking_sync *sync);


#ifdef __cplusplus
} // extern "C"
#endif
