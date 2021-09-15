// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Small utility for keeping track of the history of an xrt_space_relation, ie. for knowing where a HMD or
 * controller was in the past.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#include <algorithm>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "math/m_api.h"
#include "math/m_predict.h"
#include "math/m_vec3.h"
#include "os/os_time.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"
#include "xrt/xrt_defines.h"
#include "os/os_threading.h"
#include "util/u_template_historybuf.hpp"

#include "m_relation_history.h"


using namespace xrt::auxiliary::util;

struct relation_history_entry
{
	struct xrt_space_relation relation;
	uint64_t timestamp;
};

#define leng 4096
#define power2 12
#undef RH_DEBUG

struct m_relation_history
{
	HistoryBuffer<struct relation_history_entry, leng> impl;
	bool has_first_sample;
	struct os_mutex mutex;
};


extern "C" {
void
m_relation_history_create(struct m_relation_history **rh_ptr)
{
	*rh_ptr = U_TYPED_CALLOC(struct m_relation_history);
	struct m_relation_history *rh = *rh_ptr;

	rh->impl.topIdx = 0;
	rh->impl.length = 0;
	rh->has_first_sample = false;
	os_mutex_init(&rh->mutex);
#if 0
  struct xrt_space_relation first_relation = {};
  first_relation.pose.orientation.w = 1.0f; // Everything else, including tracked flags, is 0.
  m_relation_history_push(rh, &first_relation, os_monotonic_get_ns());
#endif
}

void
m_relation_history_push(struct m_relation_history *rh, struct xrt_space_relation *in_relation, uint64_t timestamp)
{
	XRT_TRACE_MARKER();
	struct relation_history_entry rhe;
	rhe.relation = *in_relation;
	rhe.timestamp = timestamp;
	os_mutex_lock(&rh->mutex);
	// Don't evaluate the second condition if the length is 0 - rh->impl[0] will be NULL!
	if ((!rh->has_first_sample) || (rhe.timestamp > rh->impl[0]->timestamp)) {
		// Everything explodes if the timestamps in relation_history aren't monotonically increasing. If we get
		// a timestamp that's before the most recent timestamp in the buffer, just don't put it in the history.
		rh->impl.push(rhe);
	}
	rh->has_first_sample = true;
	os_mutex_unlock(&rh->mutex);
}

void
m_relation_history_get(struct m_relation_history *rh, struct xrt_space_relation *out_relation, uint64_t at_timestamp_ns)
{
	XRT_TRACE_MARKER();
	os_mutex_lock(&rh->mutex);
	if (rh->has_first_sample == 0) {
		// Do nothing. You push nothing to the buffer you get nothing from the buffer.
		goto end;
	}

	{
		uint64_t oldest_in_buffer = rh->impl[rh->impl.length - 1]->timestamp;
		uint64_t newest_in_buffer = rh->impl[0]->timestamp;

		if (at_timestamp_ns > newest_in_buffer) {
			// The desired timestamp is after what our buffer contains.
			// Aka pose-prediction.
			int64_t diff_prediction_ns = 0;
			diff_prediction_ns = at_timestamp_ns - newest_in_buffer;
			double delta_s = time_ns_to_s(diff_prediction_ns);
#ifdef RH_DEBUG
			U_LOG_E("Extrapolating %f s after the head of the buffer!", delta_s);
#endif
			m_predict_relation(&rh->impl[0]->relation, delta_s, out_relation);
			goto end;

		} else if (at_timestamp_ns < oldest_in_buffer) {
			// The desired timestamp is before what our buffer contains.
			// Aka a weird edge case where somebody asks for a really old pose and we do our best.
			int64_t diff_prediction_ns = 0;
			diff_prediction_ns = at_timestamp_ns - oldest_in_buffer;
			double delta_s = time_ns_to_s(diff_prediction_ns);
#ifdef RH_DEBUG
			U_LOG_E("Extrapolating %f s before the tail of the buffer!", delta_s);
#endif
			m_predict_relation(&rh->impl[rh->impl.length - 1]->relation, delta_s, out_relation);
			goto end;
		}
#ifdef RH_DEBUG
		U_LOG_E("Interpolating within buffer!");
#endif
#if 0
		// Very slow - O(n) - but easier to read
		int idx = 0;

		for (int i = 0; i < rh->impl.length; i++) {
			if (rh->impl[i]->timestamp < at_timestamp_ns) {
				// If the entry we're looking at is before the input time
				idx = i;
				break;
			}
		}
		U_LOG_E("Correct answer is %i", idx);
#else

		// Fast - O(log(n)) - but hard to read
		int idx = leng / 2; // 2048
		int step = idx;

		for (int i = power2 - 2; i >= -1; i--) {
			uint64_t ts_after = rh->impl[idx - 1]->timestamp;
			uint64_t ts_before = rh->impl[idx]->timestamp;

			// This is a little hack because any power of two looks like 0b0001000 (with the 1 in a
			// different place for each power). Bit-shift it and it either doubles or halves. In our case it
			// halves. step should always be equivalent to pow(2,i). If it's not that's very very bad.
			step = step >> 1;
#ifdef RH_DEBUG
			assert(step == (int)pow(2, i));
#endif

			if (idx >= rh->impl.length) {
				// We'd be looking at an uninitialized value. Go back closer to the head of the buffer.
				idx -= step;
				continue;
			}
			if ((ts_before < at_timestamp_ns) && (ts_after > at_timestamp_ns)) {
				// Found what we're looking for - at_timestamp_ns is between the reading before us and
				// the reading after us. Break out of the loop
				break;
			}

			// This would mean you did the math very wrong. Doesn't happen.
			assert(i != -1);

			if (ts_after > at_timestamp_ns) {
				// the reading we're looking at is after the reading we want; go closer to the tail of
				// the buffer
				idx += step;
			} else {
				// the reading we're looking at is before the reading we want; go closer to the head of
				// the buffer
				idx -= step;
				// Random note: some day, stop using pow(). it's slow, you can do
			}
		}
#endif

		// Do the thing.
		struct xrt_space_relation before = rh->impl[idx]->relation;
		struct xrt_space_relation after = rh->impl[idx - 1]->relation;
		int64_t diff_before, diff_after = 0;
		diff_before = at_timestamp_ns - rh->impl[idx]->timestamp;
		diff_after = rh->impl[idx - 1]->timestamp - at_timestamp_ns;

		float amount_to_lerp = (float)diff_before / (float)(diff_before + diff_after);

		// Copy relation flags
		out_relation->relation_flags =
		    (enum xrt_space_relation_flags)(before.relation_flags & after.relation_flags);

		// First-order implementation - just lerp between the before and after
		out_relation->pose.position = m_vec3_lerp(before.pose.position, after.pose.position, amount_to_lerp);
		math_quat_slerp(&before.pose.orientation, &after.pose.orientation, amount_to_lerp,
		                &out_relation->pose.orientation);

		//! @todo Does this make any sense?
		out_relation->angular_velocity =
		    m_vec3_lerp(before.angular_velocity, after.angular_velocity, amount_to_lerp);
		out_relation->linear_velocity =
		    m_vec3_lerp(before.linear_velocity, after.linear_velocity, amount_to_lerp);
	}
end:
	os_mutex_unlock(&rh->mutex);
}

void
m_relation_history_destroy(struct m_relation_history **rh_ptr)
{
	struct m_relation_history *rh = *rh_ptr;
	if (rh == NULL) {
		// Do nothing, it's likely already been destroyed
		return;
	}
	os_mutex_destroy(&rh->mutex);
	free(rh);
	*rh_ptr = NULL;
}
}
