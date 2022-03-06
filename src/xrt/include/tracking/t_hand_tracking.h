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


#ifdef __cplusplus
extern "C" {
#endif


// /*!
//  * Result of a two hand tracking processing.
//  */
// struct t_hand_tracking_result_two
// {
// 	struct xrt_hand_joint_set hands[2]; // left, right
// };

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
