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

constexpr size_t BufLen = 4096;
constexpr size_t power2 = 12;

struct m_relation_history
{
	HistoryBuffer<struct relation_history_entry, BufLen> impl;
	bool has_first_sample;
	struct os_mutex mutex;
};


extern "C" {
void
m_relation_history_create(struct m_relation_history **rh_ptr)
{
	*rh_ptr = U_TYPED_CALLOC(struct m_relation_history);
	struct m_relation_history *rh = *rh_ptr;

	rh->has_first_sample = false;
	os_mutex_init(&rh->mutex);
#if 0
  struct xrt_space_relation first_relation = {};
  first_relation.pose.orientation.w = 1.0f; // Everything else, including tracked flags, is 0.
  m_relation_history_push(rh, &first_relation, os_monotonic_get_ns());
#endif
}

bool
m_relation_history_push(struct m_relation_history *rh, struct xrt_space_relation const *in_relation, uint64_t timestamp)
{
	XRT_TRACE_MARKER();
	struct relation_history_entry rhe;
	rhe.relation = *in_relation;
	rhe.timestamp = timestamp;
	bool ret = false;
	os_mutex_lock(&rh->mutex);
	// if we aren't empty, we can compare against the latest timestamp.
	if (rh->impl.empty() || rhe.timestamp > rh->impl.back().timestamp) {
		// Everything explodes if the timestamps in relation_history aren't monotonically increasing. If we get
		// a timestamp that's before the most recent timestamp in the buffer, just don't put it in the history.
		rh->impl.push_back(rhe);
		ret = true;
	}
	rh->has_first_sample = true;
	os_mutex_unlock(&rh->mutex);
	return ret;
}

enum m_relation_history_result
m_relation_history_get(struct m_relation_history *rh, uint64_t at_timestamp_ns, struct xrt_space_relation *out_relation)
{
	XRT_TRACE_MARKER();
	os_mutex_lock(&rh->mutex);
	m_relation_history_result ret = M_RELATION_HISTORY_RESULT_INVALID;
	if (rh->impl.empty() || at_timestamp_ns == 0) {
		// Do nothing. You push nothing to the buffer you get nothing from the buffer.
		goto end;
	}

	{
		uint64_t oldest_in_buffer = rh->impl.front().timestamp;
		uint64_t newest_in_buffer = rh->impl.back().timestamp;

		if (at_timestamp_ns > newest_in_buffer) {
			// The desired timestamp is after what our buffer contains.
			// Aka pose-prediction.
			int64_t diff_prediction_ns = 0;
			diff_prediction_ns = at_timestamp_ns - newest_in_buffer;
			double delta_s = time_ns_to_s(diff_prediction_ns);

			U_LOG_T("Extrapolating %f s after the head of the buffer!", delta_s);

			m_predict_relation(&rh->impl.back().relation, delta_s, out_relation);
			ret = M_RELATION_HISTORY_RESULT_PREDICTED;
			goto end;

		} else if (at_timestamp_ns < oldest_in_buffer) {
			// The desired timestamp is before what our buffer contains.
			// Aka a weird edge case where somebody asks for a really old pose and we do our best.
			int64_t diff_prediction_ns = 0;
			diff_prediction_ns = at_timestamp_ns - oldest_in_buffer;
			double delta_s = time_ns_to_s(diff_prediction_ns);
			U_LOG_T("Extrapolating %f s before the tail of the buffer!", delta_s);
			m_predict_relation(&rh->impl.front().relation, delta_s, out_relation);
			ret = M_RELATION_HISTORY_RESULT_REVERSE_PREDICTED;
			goto end;
		}
		U_LOG_T("Interpolating within buffer!");
#if 0
		// Very slow - O(n) - but easier to read
		size_t idx = 0;

		for (size_t i = 0; i < rh->impl.size(); i++) {
			auto ts = rh->impl.get_at_age(i)->timestamp;
			if (ts == at_timestamp_ns) {
				*out_relation = rh->impl.get_at_age(i)->relation;
				ret = M_RELATION_HISTORY_RESULT_EXACT;
				goto end;
			}

			if (ts < at_timestamp_ns) {
				// If the entry we're looking at is before the input time
				idx = i;
				break;
			}
		}
		U_LOG_T("Correct answer is %li", idx);
#else

		// Fast - O(log(n)) - but hard to read
		int idx = BufLen / 2; // 2048
		int step = idx;

		for (int i = power2 - 2; i >= -1; i--) {

			// This is a little hack because any power of two looks like 0b0001000 (with the 1 in a
			// different place for each power). Bit-shift it and it either doubles or halves. In our case it
			// halves. step should always be equivalent to pow(2,i). If it's not that's very very bad.
			step = step >> 1;
			assert(step == (int)pow(2, i));


			if (idx >= (int)rh->impl.size()) {
				// We'd be looking at an uninitialized value. Go back closer to the head of the buffer.
				idx -= step;
				continue;
			}
			assert(idx > 0);

			uint64_t ts_after = rh->impl.get_at_age(idx - 1)->timestamp;
			uint64_t ts_before = rh->impl.get_at_age(idx)->timestamp;
			if (ts_before == at_timestamp_ns || ts_after == at_timestamp_ns) {
				// exact match
				break;
			}
			if ((ts_before < at_timestamp_ns) && (ts_after > at_timestamp_ns)) {
				// Found what we're looking for - at_timestamp_ns is between the reading before
				// us and the reading after us. Break out of the loop
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
		struct xrt_space_relation before = rh->impl.get_at_age(idx)->relation;
		struct xrt_space_relation after = rh->impl.get_at_age(idx - 1)->relation;

		if (rh->impl.get_at_age(idx)->timestamp == at_timestamp_ns) {
			// exact match: before
			*out_relation = before;
			ret = M_RELATION_HISTORY_RESULT_EXACT;
			goto end;
		}
		if (rh->impl.get_at_age(idx - 1)->timestamp == at_timestamp_ns) {
			// exact match: after
			*out_relation = after;
			ret = M_RELATION_HISTORY_RESULT_EXACT;
			goto end;
		}
		int64_t diff_before, diff_after = 0;
		diff_before = at_timestamp_ns - rh->impl.get_at_age(idx)->timestamp;
		diff_after = rh->impl.get_at_age(idx - 1)->timestamp - at_timestamp_ns;

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
		ret = M_RELATION_HISTORY_RESULT_INTERPOLATED;
	}
end:
	os_mutex_unlock(&rh->mutex);
	return ret;
}

bool
m_relation_history_get_latest(struct m_relation_history *rh,
                              uint64_t *out_time_ns,
                              struct xrt_space_relation *out_relation)
{
	os_mutex_lock(&rh->mutex);
	if (rh->impl.empty()) {
		os_mutex_unlock(&rh->mutex);
		return false;
	}
	*out_relation = rh->impl.back().relation;
	*out_time_ns = rh->impl.back().timestamp;
	os_mutex_unlock(&rh->mutex);
	return true;
}

uint32_t
m_relation_history_get_size(const struct m_relation_history *rh)
{
	return (uint32_t)rh->impl.size();
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
