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

enum hand_tracking_output_space
{
	HT_OUTPUT_SPACE_LEFT_CAMERA,
	HT_OUTPUT_SPACE_CENTER_OF_STEREO_CAMERA,
};

enum hand_tracking_algorithm
{
	HT_ALGORITHM_MERCURY,
	HT_ALGORITHM_OLD_RGB
};

enum hand_tracking_image_boundary_type
{
	HT_IMAGE_BOUNDARY_NONE,
	HT_IMAGE_BOUNDARY_CIRCLE,
};

struct hand_tracking_image_boundary_circle
{
	// The center, in normalized 0-1 UV coordinates.
	// Should probably be between 0 and 1 in pixel coordinates.
	struct xrt_vec2 normalized_center;
	// The radius, divided by the image width.
	// For Index, should be around 0.5.
	float normalized_radius;
};

struct hand_tracking_image_boundary_info_one_view
{
	enum hand_tracking_image_boundary_type type;
	union {
		struct hand_tracking_image_boundary_circle circle;
	} boundary;
};


struct hand_tracking_image_boundary_info
{
	//!@todo Hardcoded to 2 - needs to increase as we support headsets with more cameras.
	struct hand_tracking_image_boundary_info_one_view views[2];
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

	void (*destroy)(struct t_hand_tracking_async *ht_async);
};

struct t_hand_tracking_async *
t_hand_tracking_async_default_create(struct xrt_frame_context *xfctx, struct t_hand_tracking_sync *sync);


#ifdef __cplusplus
} // extern "C"
#endif
