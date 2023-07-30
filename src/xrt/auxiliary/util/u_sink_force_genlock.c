// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  An @ref xrt_frame_sink that takes two frames, enforces gen-lock and pushes downstream in left-right order
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_misc.h"
#include "util/u_sink.h"
#include "util/u_frame.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"

#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>


/*!
 * An @ref xrt_frame_sink that takes two frames in any order, and pushes downstream in left-right order once it
 * has two frames that are close enough together. Shouldn't ever drop frames.
 *
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct u_sink_force_genlock
{
	//! Base sink for left.
	struct xrt_frame_sink left;

	//! Base sink for right.
	struct xrt_frame_sink right;

	//! For tracking on the frame context.
	struct xrt_frame_node node;

	//! The consumer of the frames that are queued.
	struct xrt_frame_sink *consumer_left;
	struct xrt_frame_sink *consumer_right;

	//! The current queued frame.
	struct xrt_frame *frames[2];

	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	//! Timestamp of the last frameset we pushed.
	int64_t last_ts;

	//! Should we keep running?
	//! currently, true upon startup, false as we're exiting.
	bool running;
};

static void *
force_genlock_mainloop(void *ptr)
{
	U_TRACE_SET_THREAD_NAME("Sink Genlock");

	struct u_sink_force_genlock *q = (struct u_sink_force_genlock *)ptr;
	struct xrt_frame *frames[2] = {NULL, NULL};

	pthread_mutex_lock(&q->mutex);

	while (q->running) {
		// Wait for both frames.
		if (q->frames[0] == NULL || q->frames[1] == NULL) {
			pthread_cond_wait(&q->cond, &q->mutex);
		}

		// if we're exiting, force_genlock_break_apart will set q->running to false, then wake this thread up.
		// In that case we should exit.
		if (!q->running) {
			break;
		}

		if (q->frames[0] == NULL || q->frames[1] == NULL) {
			continue;
		}

		SINK_TRACE_IDENT(force_genlock_frame);

		/*
		 * We need to take a reference on the current frame, this is to
		 * keep it alive during the call to the consumer should it be
		 * replaced. But we no longer need to hold onto the frame on the
		 * queue so we move the pointer.
		 */
		frames[0] = q->frames[0];
		frames[1] = q->frames[1];
		q->frames[0] = NULL;
		q->frames[1] = NULL;

		/*
		 * Check timestamps.
		 */
		int64_t diff_ns = frames[0]->timestamp - frames[1]->timestamp;
		if (diff_ns < -U_TIME_1MS_IN_NS || diff_ns > U_TIME_1MS_IN_NS) {

			U_LOG_W("Frame differ in timestamps too much! (%lli)", (long long)diff_ns);

			// Save the most recent frame.
			if (diff_ns > 0) {
				xrt_frame_reference(&q->frames[0], frames[0]);
			} else {
				xrt_frame_reference(&q->frames[1], frames[1]);
			}
			pthread_mutex_unlock(&q->mutex);

			// Don't hold the lock while releasing the frames.
			xrt_frame_reference(&frames[0], NULL);
			xrt_frame_reference(&frames[1], NULL);

			pthread_mutex_lock(&q->mutex);
			continue;
		}

		/*
		 * Unlock the mutex when we do the work, so a new frame can be
		 * queued.
		 */
		pthread_mutex_unlock(&q->mutex);

		/*
		 * Average the timestamps, SLAM systems break if they don't have the exact same timestamp.
		 * (This is not great, because on DepthAI the images *are* taken like 0.1ms apart, and we *could* expose
		 * that, but oh well.)
		 */

		int64_t ts_1 = frames[0]->timestamp;
		int64_t ts_2 = frames[1]->timestamp;

		int64_t diff = (ts_2 - ts_1);

		int64_t ts = ts_1 + (diff / 2);

		frames[0]->timestamp = ts;
		frames[1]->timestamp = ts;

		if (ts == q->last_ts) {
			U_LOG_W("Got an image frame with a duplicate timestamp! Old: %" PRId64 "; New: %" PRId64,
			        q->last_ts, ts);
		} else if (ts < q->last_ts) {
			U_LOG_W("Got an image frame with a non-monotonically-increasing timestamp! Old: %" PRId64
			        "; New: %" PRId64,
			        q->last_ts, ts);
		} else {
			// Send to the consumer, in left-right order.
			xrt_sink_push_frame(q->consumer_left, frames[0]);
			xrt_sink_push_frame(q->consumer_right, frames[1]);
		}
		/*
		 * Drop our reference - we don't need it anymore. If the consumer wants to keep it, they will have
		 * referenced it in their push_frame handler.
		 */
		xrt_frame_reference(&frames[0], NULL);
		xrt_frame_reference(&frames[1], NULL);

		// Have to lock it again.
		pthread_mutex_lock(&q->mutex);
	}

	pthread_mutex_unlock(&q->mutex);

	return NULL;
}

static void
force_genlock_left_frame(struct xrt_frame_sink *xfs, struct xrt_frame *xf)
{
	SINK_TRACE_MARKER();

	struct u_sink_force_genlock *q = container_of(xfs, struct u_sink_force_genlock, left);

	pthread_mutex_lock(&q->mutex);

	// Only schedule new frames if we are running.
	if (q->running) {
		xrt_frame_reference(&q->frames[0], xf);
	}

	// Wake up the thread, if both frames are here.
	if (q->frames[0] != NULL && q->frames[1] != NULL) {
		pthread_cond_signal(&q->cond);
	}

	pthread_mutex_unlock(&q->mutex);
}

static void
force_genlock_right_frame(struct xrt_frame_sink *xfs, struct xrt_frame *xf)
{
	SINK_TRACE_MARKER();

	struct u_sink_force_genlock *q = container_of(xfs, struct u_sink_force_genlock, right);

	pthread_mutex_lock(&q->mutex);

	// Only schedule new frames if we are running.
	if (q->running) {
		xrt_frame_reference(&q->frames[1], xf);
	}

	// Wake up the thread, if both frames are here.
	if (q->frames[0] != NULL && q->frames[1] != NULL) {
		pthread_cond_signal(&q->cond);
	}

	pthread_mutex_unlock(&q->mutex);
}

static void
force_genlock_break_apart(struct xrt_frame_node *node)
{
	struct u_sink_force_genlock *q = container_of(node, struct u_sink_force_genlock, node);
	void *retval = NULL;

	// The fields are protected.
	pthread_mutex_lock(&q->mutex);

	// Stop the thread and inhibit any new frames to be added to the queue.
	q->running = false;

	// Release any frame waiting for submission.
	xrt_frame_reference(&q->frames[0], NULL);
	xrt_frame_reference(&q->frames[1], NULL);

	// Wake up the thread.
	pthread_cond_signal(&q->cond);

	// No longer need to protect fields.
	pthread_mutex_unlock(&q->mutex);

	// Wait for thread to finish.
	pthread_join(q->thread, &retval);
}

static void
force_genlock_destroy(struct xrt_frame_node *node)
{
	struct u_sink_force_genlock *q = container_of(node, struct u_sink_force_genlock, node);

	// Destroy resources.
	pthread_mutex_destroy(&q->mutex);
	pthread_cond_destroy(&q->cond);
	free(q);
}


/*
 *
 * Exported functions.
 *
 */

bool
u_sink_force_genlock_create(struct xrt_frame_context *xfctx,
                            struct xrt_frame_sink *downstream_left,
                            struct xrt_frame_sink *downstream_right,
                            struct xrt_frame_sink **out_left_xfs,
                            struct xrt_frame_sink **out_right_xfs)
{
	struct u_sink_force_genlock *q = U_TYPED_CALLOC(struct u_sink_force_genlock);
	int ret = 0;

	q->left.push_frame = force_genlock_left_frame;
	q->right.push_frame = force_genlock_right_frame;
	q->node.break_apart = force_genlock_break_apart;
	q->node.destroy = force_genlock_destroy;
	q->consumer_left = downstream_left;
	q->consumer_right = downstream_right;
	q->running = true;

	ret = pthread_mutex_init(&q->mutex, NULL);
	if (ret != 0) {
		free(q);
		return false;
	}

	ret = pthread_cond_init(&q->cond, NULL);
	if (ret) {
		pthread_mutex_destroy(&q->mutex);
		free(q);
		return false;
	}

	ret = pthread_create(&q->thread, NULL, force_genlock_mainloop, q);
	if (ret != 0) {
		pthread_cond_destroy(&q->cond);
		pthread_mutex_destroy(&q->mutex);
		free(q);
		return false;
	}

	xrt_frame_context_add(xfctx, &q->node);

	*out_left_xfs = &q->left;
	*out_right_xfs = &q->right;

	return true;
}
