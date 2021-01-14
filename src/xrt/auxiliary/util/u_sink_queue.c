// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  An @ref xrt_frame_sink queue.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_misc.h"
#include "util/u_sink.h"

#include <stdio.h>
#include <pthread.h>


/*!
 * An @ref xrt_frame_sink queue.
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct u_sink_queue
{
	struct xrt_frame_sink base;
	struct xrt_frame_node node;

	struct xrt_frame_sink *consumer;
	struct xrt_frame *frame;

	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	struct
	{
		uint64_t current;
		uint64_t last;
	} seq;

	bool running;
};

static void *
sque_run(void *ptr)
{
	struct u_sink_queue *q = (struct u_sink_queue *)ptr;
	struct xrt_frame *frame = NULL;

	pthread_mutex_lock(&q->mutex);

	while (q->running) {

		// No new frame, wait.
		if (q->seq.last >= q->seq.current) {
			pthread_cond_wait(&q->cond, &q->mutex);
		}

		// Where we woken up to turn of.
		if (!q->running) {
			break;
		}

		// Just in case.
		if (q->seq.last >= q->seq.current || q->frame == NULL) {
			continue;
		}

		// We have a new frame, send it out.
		q->seq.last = q->seq.current;

		// Take a reference on the current frame, this keeps it alive
		// if it is replaced during the consumer processing it, but
		// we no longer need to hold onto the frame on the queue we
		// just move the pointer.
		frame = q->frame;
		q->frame = NULL;

		// Unlock the mutex when we do the work.
		pthread_mutex_unlock(&q->mutex);

		// Send to the consumer that does the work.
		q->consumer->push_frame(q->consumer, frame);

		// Drop our reference we don't need it anymore,
		// or it's held on the queue.
		xrt_frame_reference(&frame, NULL);

		// Have to lock it again.
		pthread_mutex_lock(&q->mutex);
	}

	pthread_mutex_unlock(&q->mutex);

	return NULL;
}

static void
sque_frame(struct xrt_frame_sink *xfs, struct xrt_frame *xf)
{
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
break_apart(struct xrt_frame_node *node)
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
destroy(struct xrt_frame_node *node)
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
u_sink_queue_create(struct xrt_frame_context *xfctx, struct xrt_frame_sink *downstream, struct xrt_frame_sink **out_xfs)
{
	struct u_sink_queue *q = U_TYPED_CALLOC(struct u_sink_queue);
	int ret = 0;

	q->base.push_frame = sque_frame;
	q->node.break_apart = break_apart;
	q->node.destroy = destroy;
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

	ret = pthread_create(&q->thread, NULL, sque_run, q);
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
