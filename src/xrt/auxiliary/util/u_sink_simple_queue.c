// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  An @ref xrt_frame_sink queue.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_misc.h"
#include "util/u_sink.h"
#include "util/u_trace_marker.h"

#include <stdio.h>
#include <pthread.h>


/*!
 * An @ref xrt_frame_sink queue, any frames received will be pushed to the
 * downstream consumer on the queue thread. Will drop frames should multiple
 * frames be queued up.
 *
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct u_sink_queue
{
	//! Base sink.
	struct xrt_frame_sink base;
	//! For tracking on the frame context.
	struct xrt_frame_node node;

	//! The consumer of the frames that are queued.
	struct xrt_frame_sink *consumer;

	//! The current queued frame.
	struct xrt_frame *frame;

	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	struct
	{
		uint64_t current;
		uint64_t last;
	} seq;

	//! Should we keep running.
	bool running;
};

static void *
queue_mainloop(void *ptr)
{
	U_TRACE_SET_THREAD_NAME("Sink Queue");

	struct u_sink_queue *q = (struct u_sink_queue *)ptr;
	struct xrt_frame *frame = NULL;

	pthread_mutex_lock(&q->mutex);

	while (q->running) {

		// No new frame, wait.
		if (q->seq.last >= q->seq.current) {
			pthread_cond_wait(&q->cond, &q->mutex);
		}

		// Where we woken up to turn off.
		if (!q->running) {
			break;
		}

		// Just in case.
		if (q->seq.last >= q->seq.current || q->frame == NULL) {
			continue;
		}

		SINK_TRACE_IDENT(queue_frame);

		// We have a new frame, send it out.
		q->seq.last = q->seq.current;

		/*
		 * We need to take a reference on the current frame, this is to
		 * keep it alive during the call to the consumer should it be
		 * replaced. But we no longer need to hold onto the frame on the
		 * queue so we move the pointer.
		 */
		frame = q->frame;
		q->frame = NULL;

		/*
		 * Unlock the mutex when we do the work, so a new frame can be
		 * queued.
		 */
		pthread_mutex_unlock(&q->mutex);

		// Send to the consumer that does the work.
		q->consumer->push_frame(q->consumer, frame);

		/*
		 * Drop our reference we don't need it anymore, or it's held by
		 * the consumer.
		 */
		xrt_frame_reference(&frame, NULL);

		// Have to lock it again.
		pthread_mutex_lock(&q->mutex);
	}

	pthread_mutex_unlock(&q->mutex);

	return NULL;
}

static void
queue_frame(struct xrt_frame_sink *xfs, struct xrt_frame *xf)
{
	SINK_TRACE_MARKER();

	struct u_sink_queue *q = (struct u_sink_queue *)xfs;

	pthread_mutex_lock(&q->mutex);

	// Only schedule new frames if we are running.
	if (q->running) {
		q->seq.current++;
		xrt_frame_reference(&q->frame, xf);
	}

	// Wake up the thread.
	pthread_cond_signal(&q->cond);

	pthread_mutex_unlock(&q->mutex);
}

static void
queue_break_apart(struct xrt_frame_node *node)
{
	struct u_sink_queue *q = container_of(node, struct u_sink_queue, node);
	void *retval = NULL;

	// The fields are protected.
	pthread_mutex_lock(&q->mutex);

	// Stop the thread and inhibit any new frames to be added to the queue.
	q->running = false;

	// Release any frame waiting for submission.
	xrt_frame_reference(&q->frame, NULL);

	// Wake up the thread.
	pthread_cond_signal(&q->cond);

	// No longer need to protect fields.
	pthread_mutex_unlock(&q->mutex);

	// Wait for thread to finish.
	pthread_join(q->thread, &retval);
}

static void
queue_destroy(struct xrt_frame_node *node)
{
	struct u_sink_queue *q = container_of(node, struct u_sink_queue, node);

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
u_sink_simple_queue_create(struct xrt_frame_context *xfctx,
                           struct xrt_frame_sink *downstream,
                           struct xrt_frame_sink **out_xfs)
{
	struct u_sink_queue *q = U_TYPED_CALLOC(struct u_sink_queue);
	int ret = 0;

	q->base.push_frame = queue_frame;
	q->node.break_apart = queue_break_apart;
	q->node.destroy = queue_destroy;
	q->consumer = downstream;
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

	ret = pthread_create(&q->thread, NULL, queue_mainloop, q);
	if (ret != 0) {
		pthread_cond_destroy(&q->cond);
		pthread_mutex_destroy(&q->mutex);
		free(q);
		return false;
	}

	xrt_frame_context_add(xfctx, &q->node);

	*out_xfs = &q->base;

	return true;
}
