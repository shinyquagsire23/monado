// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  An @ref xrt_frame_sink that combines two frames into a stereo frame.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <moses@collabora.com>
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
 * An @ref xrt_frame_sink combiner, frames pushed to the left and right side will be combined into one @ref xrt_frame
 * with format XRT_STEREO_FORMAT_SBS. Will drop stale frames if the combining work takes too long.
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
combine_frames_l8(struct xrt_frame *l, struct xrt_frame *r, struct xrt_frame *f)
{
	SINK_TRACE_MARKER();


	uint32_t height = l->height;

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

static void
combine_frames_r8g8b8(struct xrt_frame *l, struct xrt_frame *r, struct xrt_frame *f)
{
	SINK_TRACE_MARKER();

	uint32_t height = l->height;

	for (uint32_t y = 0; y < height; y++) {
		uint8_t *dst = f->data + f->stride * y;
		uint8_t *src = l->data + l->stride * y;

		for (uint32_t x = 0; x < l->width * 3; x++) {
			*dst++ = *src++;
		}

		dst = f->data + f->stride * y + l->width * 3;
		src = r->data + r->stride * y;
		for (uint32_t x = 0; x < r->width * 3; x++) {
			*dst++ = *src++;
		}
	}
}

static void
combine_frames(struct xrt_frame *l, struct xrt_frame *r, struct xrt_frame **out_frame)
{
	SINK_TRACE_MARKER();

	assert(l->width == r->width);
	assert(l->height == r->height);
	assert(l->format == r->format);
	assert((l->format == XRT_FORMAT_L8) || (l->format == XRT_FORMAT_R8G8B8));

	int64_t diff_ns = l->timestamp - r->timestamp;
	uint32_t height = l->height;
	uint32_t width = l->width + r->width;
	enum xrt_format format = l->format;

	u_frame_create_one_off(format, width, height, out_frame);

	struct xrt_frame *f = *out_frame;
	f->timestamp = l->timestamp - (diff_ns / 2); // Middle of both frames.
	f->stereo_format = XRT_STEREO_FORMAT_SBS;
	f->source_sequence = l->source_sequence;

	switch (l->format) {
	case XRT_FORMAT_L8: {
		combine_frames_l8(l, r, f);
		break;
	}
	case XRT_FORMAT_R8G8B8: {
		combine_frames_r8g8b8(l, r, f);
		break;
	}
	default: assert(!"Unimplemented!");
	}
#if 0
	// So that we can test if this works on a really slow computer
	os_nanosleep(0.1f * U_TIME_1S_IN_NS);
#endif
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

		// the right frame can be not null if the combiner is dropping frames and we've received another
		// left-right push as it was still running.
		xrt_frame_reference(&q->frames[1], NULL);
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

	// If both frames are here, do the work!
	// (Yes, this push_frame will block, and so will combiner_left_frame as it's waiting for the work to complete.
	// It's okay, u_sink_force_genlock does the async/frame-dropping stuff for us.)
	if (q->frames[0] != NULL && q->frames[1] != NULL) {
		struct xrt_frame *frames[2] = {NULL, NULL};
		frames[0] = q->frames[0];
		frames[1] = q->frames[1];
		q->frames[0] = NULL;
		q->frames[1] = NULL;
		/*
		 * Check timestamps.
		 */
		XRT_MAYBE_UNUSED int64_t diff_ns = frames[0]->timestamp - frames[1]->timestamp;


		// u_sink_force_genlock should have done this for us already
		assert(!(diff_ns < -U_TIME_1MS_IN_NS || diff_ns > U_TIME_1MS_IN_NS));

		struct xrt_frame *frame = NULL;
		combine_frames(frames[0], frames[1], &frame);

		// Send to the consumer that does the work.
		xrt_sink_push_frame(q->consumer, frame);

		/*
		 * Drop our reference we don't need it anymore, or it's held by
		 * the consumer.
		 */
		xrt_frame_reference(&frame, NULL);
		xrt_frame_reference(&frames[0], NULL);
		xrt_frame_reference(&frames[1], NULL);

	} else {
		U_LOG_W("Right frame pushed with no left frame");
	}

	pthread_mutex_unlock(&q->mutex);
}

static void
combiner_break_apart(struct xrt_frame_node *node)
{
	struct u_sink_combiner *q = container_of(node, struct u_sink_combiner, node);

	// The fields are protected.
	pthread_mutex_lock(&q->mutex);

	// Stop the thread and inhibit any new frames to be added to the queue.
	q->running = false;

	// Release any frame waiting for submission.
	xrt_frame_reference(&q->frames[0], NULL);
	xrt_frame_reference(&q->frames[1], NULL);

	// No longer need to protect fields.
	pthread_mutex_unlock(&q->mutex);
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

	// If you remove this, this sink will block for some time after you push the left frame while copying the data.
	// Only remove this if you're sure that's okay.
	u_sink_force_genlock_create(xfctx, &q->left, &q->right, out_left_xfs, out_right_xfs);

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

	xrt_frame_context_add(xfctx, &q->node);


	return true;
}
