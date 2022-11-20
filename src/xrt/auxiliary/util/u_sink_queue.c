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

struct u_sink_queue_elem
{
	struct xrt_frame *frame;
	struct u_sink_queue_elem *next;
};

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

	//! Front of the queue (oldest frame, first to be consumed)
	struct u_sink_queue_elem *front;

	//! Back of the queue (newest frame, back->next is always null)
	struct u_sink_queue_elem *back;

	//! Number of currently enqueued frames
	uint64_t size;

	//! Max amount of frames before dropping new ones. 0 means unbounded.
	uint64_t max_size;

	pthread_t thread;
	pthread_mutex_t mutex;

	//! So we can wake the mainloop up
	pthread_cond_t cond;

	//! Should we keep running.
	bool running;
};

//! Call with q->mutex locked.
static bool
queue_is_empty(struct u_sink_queue *q)
{
	return q->size == 0;
}

//! Call with q->mutex locked.
static bool
queue_is_full(struct u_sink_queue *q)
{
	bool is_unbounded = q->max_size == 0;
	return q->size >= q->max_size && !is_unbounded;
}

//! Pops the oldest frame, reference counting unchanged.
//! Call with q->mutex locked.
static struct xrt_frame *
queue_pop(struct u_sink_queue *q)
{
	assert(!queue_is_empty(q));
	struct xrt_frame *frame = q->front->frame;
	struct u_sink_queue_elem *old_front = q->front;
	q->front = q->front->next;
	free(old_front);
	q->size--;
	if (q->front == NULL) {
		assert(queue_is_empty(q));
		q->back = NULL;
	}
	return frame;
}

//! Tries to push a frame and increases its reference count.
//! Call with q->mutex locked.
static bool
queue_try_refpush(struct u_sink_queue *q, struct xrt_frame *xf)
{
	if (queue_is_full(q)) {
		return false;
	}
	struct u_sink_queue_elem *elem = U_TYPED_CALLOC(struct u_sink_queue_elem);
	xrt_frame_reference(&elem->frame, xf);
	elem->next = NULL;
	if (q->back == NULL) { // First frame
		q->front = elem;
	} else { // Next frame
		q->back->next = elem;
	}
	q->back = elem;
	q->size++;
	return true;
}

//! Clears the queue and unreferences all of its frames.
//! Call with q->mutex locked.
static void
queue_refclear(struct u_sink_queue *q)
{
	while (!queue_is_empty(q)) {
		assert((q->size > 1) ^ (q->front == q->back));
		struct xrt_frame *xf = queue_pop(q);
		xrt_frame_reference(&xf, NULL);
	}
}

static void *
queue_mainloop(void *ptr)
{
	U_TRACE_SET_THREAD_NAME("Sink Queue");

	struct u_sink_queue *q = (struct u_sink_queue *)ptr;
	struct xrt_frame *frame = NULL;

	pthread_mutex_lock(&q->mutex);

	while (q->running) {

		// No new frame, wait.
		if (queue_is_empty(q)) {
			pthread_cond_wait(&q->cond, &q->mutex);
		}

		// In this case, queue_break_apart woke us up to turn us off.
		if (!q->running) {
			break;
		}

		if (queue_is_empty(q)) {
			continue;
		}

		SINK_TRACE_IDENT(queue_frame);

		/*
		 * Dequeue frame.
		 * We need to take a reference on the current frame, this is to
		 * keep it alive during the call to the consumer should it be
		 * replaced. But we no longer need to hold onto the frame on the
		 * queue so we dequeue it.
		 */
		frame = queue_pop(q);

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
		queue_try_refpush(q, xf);
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
	queue_refclear(q);

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
u_sink_queue_create(struct xrt_frame_context *xfctx,
                    uint64_t max_size,
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

	q->size = 0;
	q->max_size = max_size;

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
