// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking driver code.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#include "tracking/t_hand_tracking.h"
#include "util/u_misc.h"
#include "util/u_trace_marker.h"
#include "util/u_logging.h"
#include "util/u_var.h"
#include "os/os_threading.h"

#include "math/m_space.h"
#include "math/m_relation_history.h"


//!@todo Definitely needs a destroy function, will leak a ton.

struct ht_async_impl
{
	struct t_hand_tracking_async base;

	struct t_hand_tracking_sync *provider;

	struct xrt_frame *frames[2];

	bool use_prediction;
	struct u_var_draggable_f32 prediction_offset_ms;

	struct
	{
		struct xrt_hand_joint_set hands[2];
		uint64_t timestamp;
	} working;

	struct
	{
		struct os_mutex mutex;
		struct xrt_hand_joint_set hands[2];
		struct m_relation_history *relation_hist[2];
		uint64_t timestamp;
	} present;

	// in here:
	// mutex is so that the mainloop and two push_frames don't fight over referencing frames;
	// cond is so that we can wake up the mainloop at certain times;
	// running is so we can stop the thread when Monado exits
	struct os_thread_helper mainloop;

	volatile bool hand_tracking_work_active;
};

static inline struct ht_async_impl *
ht_async_impl(struct t_hand_tracking_async *base)
{
	return (struct ht_async_impl *)base;
}

static void *
ht_async_mainloop(void *ptr)
{
	XRT_TRACE_MARKER();

	struct ht_async_impl *hta = (struct ht_async_impl *)ptr;

	os_thread_helper_lock(&hta->mainloop);

	while (os_thread_helper_is_running_locked(&hta->mainloop)) {

		// No new frame, wait.
		if (hta->frames[0] == NULL && hta->frames[1] == NULL) {
			os_thread_helper_wait_locked(&hta->mainloop);

			/*
			 * Loop back to the top to check if we should stop,
			 * also handles spurious wakeups by re-checking the
			 * condition in the if case. Essentially two loops.
			 */
			continue;
		}

		os_thread_helper_unlock(&hta->mainloop);

		t_ht_sync_process(hta->provider, hta->frames[0], hta->frames[1], &hta->working.hands[0],
		                  &hta->working.hands[1], &hta->working.timestamp);

		xrt_frame_reference(&hta->frames[0], NULL);
		xrt_frame_reference(&hta->frames[1], NULL);
		os_mutex_lock(&hta->present.mutex);
		hta->present.timestamp = hta->working.timestamp;
		for (int i = 0; i < 2; i++) {
			hta->present.hands[i] = hta->working.hands[i];

			struct xrt_space_relation wrist_rel =
			    hta->working.hands[i].values.hand_joint_set_default[XRT_HAND_JOINT_WRIST].relation;

			m_relation_history_estimate_motion(hta->present.relation_hist[i], //
			                                   &wrist_rel,                    //
			                                   hta->working.timestamp,        //
			                                   &wrist_rel);
			m_relation_history_push(hta->present.relation_hist[i], &wrist_rel, hta->working.timestamp);
		}
		os_mutex_unlock(&hta->present.mutex);

		hta->hand_tracking_work_active = false;

		// Have to lock it again.
		os_thread_helper_lock(&hta->mainloop);
	}

	os_thread_helper_unlock(&hta->mainloop);

	return NULL;
}

static void
ht_async_receive_left(struct xrt_frame_sink *sink, struct xrt_frame *frame)
{
	struct ht_async_impl *hta = ht_async_impl(container_of(sink, struct t_hand_tracking_async, left));
	if (hta->hand_tracking_work_active) {
		// Throw away this frame
		return;
	}
	assert(hta->frames[0] == NULL);
	xrt_frame_reference(&hta->frames[0], frame);
}

static void
ht_async_receive_right(struct xrt_frame_sink *sink, struct xrt_frame *frame)
{
	struct ht_async_impl *hta = ht_async_impl(container_of(sink, struct t_hand_tracking_async, right));
	if (hta->hand_tracking_work_active || hta->frames[0] == NULL) {
		// Throw away this frame - either the hand tracking work is running now,
		// or it was a very short time ago, and ht_async_receive_left threw away its frame
		// or there's some other bug where left isn't pushed before right.
		return;
	}
	assert(hta->frames[0] != NULL);
	assert(hta->frames[1] == NULL);
	xrt_frame_reference(&hta->frames[1], frame);
	hta->hand_tracking_work_active = true;
	// hta->working.timestamp = frame->timestamp;
	os_thread_helper_lock(&hta->mainloop);
	os_thread_helper_signal_locked(&hta->mainloop);
	os_thread_helper_unlock(&hta->mainloop);
}

void
ht_async_get_hand(struct t_hand_tracking_async *ht_async,
                  enum xrt_input_name name,
                  uint64_t desired_timestamp_ns,
                  struct xrt_hand_joint_set *out_value,
                  uint64_t *out_timestamp_ns)
{
	struct ht_async_impl *hta = ht_async_impl(ht_async);
	assert(name == XRT_INPUT_GENERIC_HAND_TRACKING_LEFT || name == XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT);

	int idx = 0;
	if (name == XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
		idx = 1;
	}

	os_mutex_lock(&hta->present.mutex);

	struct xrt_hand_joint_set latest_hand = hta->present.hands[idx];

	if (!hta->use_prediction) {
		*out_value = latest_hand;
		*out_timestamp_ns = hta->present.timestamp;
		os_mutex_unlock(&hta->present.mutex);
		return;
	}

	double prediction_offset_ns = (double)hta->prediction_offset_ms.val * (double)U_TIME_1MS_IN_NS;

	desired_timestamp_ns += (uint64_t)prediction_offset_ns;

	struct xrt_space_relation predicted_wrist;
	m_relation_history_get(hta->present.relation_hist[idx], desired_timestamp_ns, &predicted_wrist);

	os_mutex_unlock(&hta->present.mutex);

	struct xrt_space_relation latest_wrist =
	    latest_hand.values.hand_joint_set_default[XRT_HAND_JOINT_WRIST].relation;

	*out_value = latest_hand;

	// apply the pose change from the latest wrist to the predicted wrist
	// to all the joints on the hand.

	//!@optimize We could slightly reduce the total number of transforms by putting some of this in
	//! ht_async_mainloop
	for (int i = 0; i < XRT_HAND_JOINT_COUNT; i++) {
		struct xrt_relation_chain xrc = {0};
		m_relation_chain_push_relation(&xrc, &latest_hand.values.hand_joint_set_default[i].relation);
		m_relation_chain_push_inverted_relation(&xrc, &latest_wrist);
		m_relation_chain_push_relation(&xrc, &predicted_wrist);
		m_relation_chain_resolve(&xrc, &out_value->values.hand_joint_set_default[i].relation);
	}

	*out_timestamp_ns = desired_timestamp_ns;
}

void
ht_async_break_apart(struct xrt_frame_node *node)
{
	struct ht_async_impl *hta = ht_async_impl(container_of(node, struct t_hand_tracking_async, node));
	os_thread_helper_stop_and_wait(&hta->mainloop);
}

void
ht_async_destroy(struct xrt_frame_node *node)
{
	struct ht_async_impl *hta = ht_async_impl(container_of(node, struct t_hand_tracking_async, node));
	os_thread_helper_destroy(&hta->mainloop);
	os_mutex_destroy(&hta->present.mutex);

	t_ht_sync_destroy(&hta->provider);

	for (int i = 0; i < 2; i++) {
		m_relation_history_destroy(&hta->present.relation_hist[i]);
	}

	free(hta);
}

struct t_hand_tracking_async *
t_hand_tracking_async_default_create(struct xrt_frame_context *xfctx, struct t_hand_tracking_sync *sync)
{
	struct ht_async_impl *hta = U_TYPED_CALLOC(struct ht_async_impl);
	hta->base.left.push_frame = ht_async_receive_left;
	hta->base.right.push_frame = ht_async_receive_right;
	hta->base.sinks.left = &hta->base.left;
	hta->base.sinks.right = &hta->base.right;
	hta->base.node.break_apart = ht_async_break_apart;
	hta->base.node.destroy = ht_async_destroy;
	hta->base.get_hand = ht_async_get_hand;

	hta->provider = sync;

	for (int i = 0; i < 2; i++) {
		m_relation_history_create(&hta->present.relation_hist[i]);
	}

	u_var_add_root(hta, "Hand-tracking async shim!", 0);

	//!@todo We came up with this value just by seeing what worked - with Index and WMR, we'd be around 40ms late by
	//! the time the camera frames arrived and were processed.

	// We _really_ need a way to calibrate this live - something like an exponential filter that looks at the
	// typical maximum time between the time at which we were asked for a sample and most recent processed sample
	// timestamp.

	hta->prediction_offset_ms.val = -40;
	hta->prediction_offset_ms.step = 0.5;

	hta->use_prediction = true;

	// No need to enforce limits, although generally around -40 is what you want.
	hta->prediction_offset_ms.min = -1000000;
	hta->prediction_offset_ms.max = 1000000;

	u_var_add_bool(hta, &hta->use_prediction, "Predict wrist movement");
	u_var_add_draggable_f32(hta, &hta->prediction_offset_ms, "Amount to time-travel (ms)");


	os_mutex_init(&hta->present.mutex);
	os_thread_helper_init(&hta->mainloop);
	os_thread_helper_start(&hta->mainloop, ht_async_mainloop, hta);
	xrt_frame_context_add(xfctx, &hta->base.node);

	return &hta->base;
}
