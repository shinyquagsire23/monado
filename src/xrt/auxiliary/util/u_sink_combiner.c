// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  An @ref xrt_frame_sink that combines two frames into a stereo frame.
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


/*!
 * An @ref xrt_frame_sink queue, any frames received will be pushed to the
 * downstream consumer on the queue thread. Will drop frames should multiple
 * frames be queued up.
 *
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct u_sink_combiner
{
	//! Base sink for left.
	struct xrt_frame_sink left;

	//! Base sink for right.
	struct xrt_frame_sink right;

	//! For tracking on the frame context.
	struct xrt_frame_node node;

	//! The consumer of the frames that are queued.
	struct xrt_frame_sink *consumer;

	//! The current queued frame.
	struct xrt_frame *frames[2];

	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	//! Should we keep running.
	bool running;
};

static void
combine_frames(struct xrt_frame *l, struct xrt_frame *r, struct xrt_frame **out_frame)
{
	SINK_TRACE_MARKER();

	assert(l->width == r->width);
	assert(l->height == r->height);
	assert(l->format == r->format);
	assert(l->format == XRT_FORMAT_L8);

	int64_t diff_ns = l->timestamp - r->timestamp;
	uint32_t height = l->height;
	uint32_t width = l->width + r->width;
	enum xrt_format format = l->format;

	u_frame_create_one_off(format, width, height, out_frame);

	struct xrt_frame *f = *out_frame;
	f->timestamp = l->timestamp - (diff_ns / 2); // Middle of both frames.
	f->stereo_format = XRT_STEREO_FORMAT_SBS;
	f->source_sequence = l->source_sequence;

	SINK_TRACE_IDENT(combine_frames_copy);

	for (uint32_t y = 0; y < height; y++) {
		uint8_t *dst = f->data + f->stride * y;
		uint8_t *src = l->data + l->stride * y;

		for (uint32_t x = 0; x < l->width; x++) {
			*dst++ = *src++;
		}

		dst = f->data + f->stride * y + l->width;
		src = r->data + r->stride * y;
		for (uint32_t x = 0; x < r->width; x++) {
			*dst++ = *src++;
		}
	}
}

static void *
combiner_mainloop(void *ptr)
{
	SINK_TRACE_MARKER();

	struct u_sink_combiner *q = (struct u_sink_combiner *)ptr;
	struct xrt_frame *frames[2] = {NULL, NULL};

	pthread_mutex_lock(&q->mutex);

	while (q->running) {
		// Wait for both frames.
		if (q->frames[0] == NULL || q->frames[1] == NULL) {
			pthread_cond_wait(&q->cond, &q->mutex);
		}

		// Where we woken up to turn off.
		if (!q->running) {
			break;
		}

		// Just in case.
		if (q->frames[0] == NULL || q->frames[1] == NULL) {
			continue;
		}

		SINK_TRACE_IDENT(combiner_frame);

		/*
		 * We need to take a reference on the current frame, this is to
		 * keep it alive during the call to the consumer should it be
		 * replaced. But we no longer need to hold onto the frame on the
		 * queue so we just move the pointer.
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

		struct xrt_frame *frame = NULL;
		combine_frames(frames[0], frames[1], &frame);

		// Send to the consumer that does the work.
		q->consumer->push_frame(q->consumer, frame);

		/*
		 * Drop our reference we don't need it anymore, or it's held by
		 * the consumer.
		 */
		xrt_frame_reference(&frame, NULL);
		xrt_frame_reference(&frames[0], NULL);
		xrt_frame_reference(&frames[1], NULL);

		// Have to lock it again.
		pthread_mutex_lock(&q->mutex);
	}

	pthread_mutex_unlock(&q->mutex);

	return NULL;
}

static void
combiner_left_frame(struct xrt_frame_sink *xfs, struct xrt_frame *xf)
{
	SINK_TRACE_MARKER();

	struct u_sink_combiner *q = container_of(xfs, struct u_sink_combiner, left);

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
combiner_right_frame(struct xrt_frame_sink *xfs, struct xrt_frame *xf)
{
	SINK_TRACE_MARKER();

	struct u_sink_combiner *q = container_of(xfs, struct u_sink_combiner, right);

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
combiner_break_apart(struct xrt_frame_node *node)
{
	struct u_sink_combiner *q = container_of(node, struct u_sink_combiner, node);
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
combiner_destroy(struct xrt_frame_node *node)
{
	struct u_sink_combiner *q = container_of(node, struct u_sink_combiner, node);

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
u_sink_combiner_create(struct xrt_frame_context *xfctx,
                       struct xrt_frame_sink *downstream,
                       struct xrt_frame_sink **out_left_xfs,
                       struct xrt_frame_sink **out_right_xfs)
{
	struct u_sink_combiner *q = U_TYPED_CALLOC(struct u_sink_combiner);
	int ret = 0;

	q->left.push_frame = combiner_left_frame;
	q->right.push_frame = combiner_right_frame;
	q->node.break_apart = combiner_break_apart;
	q->node.destroy = combiner_destroy;
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

	ret = pthread_create(&q->thread, NULL, combiner_mainloop, q);
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
