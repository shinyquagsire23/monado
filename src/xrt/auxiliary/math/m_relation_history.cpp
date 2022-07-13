// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Small utility for keeping track of the history of an xrt_space_relation, ie. for knowing where a HMD or
 * controller was in the past.
 * @author Moses Turner <moses@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#include "m_relation_history.h"

#include "math/m_api.h"
#include "math/m_predict.h"
#include "math/m_vec3.h"
#include "os/os_time.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"
#include "xrt/xrt_defines.h"
#include "os/os_threading.h"
#include "util/u_template_historybuf.hpp"

#include <memory>
#include <algorithm>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <mutex>

using namespace xrt::auxiliary::util;
namespace os = xrt::auxiliary::os;

struct relation_history_entry
{
	struct xrt_space_relation relation;
	uint64_t timestamp;
};

static constexpr size_t BufLen = 4096;

struct m_relation_history
{
	HistoryBuffer<struct relation_history_entry, BufLen> impl;
	os::Mutex mutex;
};


void
m_relation_history_create(struct m_relation_history **rh_ptr)
{
	auto ret = std::make_unique<m_relation_history>();
	*rh_ptr = ret.release();
}

bool
m_relation_history_push(struct m_relation_history *rh, struct xrt_space_relation const *in_relation, uint64_t timestamp)
{
	XRT_TRACE_MARKER();
	struct relation_history_entry rhe;
	rhe.relation = *in_relation;
	rhe.timestamp = timestamp;
	bool ret = false;
	std::unique_lock<os::Mutex> lock(rh->mutex);
	try {
		// if we aren't empty, we can compare against the latest timestamp.
		if (rh->impl.empty() || rhe.timestamp > rh->impl.back().timestamp) {
			// Everything explodes if the timestamps in relation_history aren't monotonically increasing. If
			// we get a timestamp that's before the most recent timestamp in the buffer, don't put it
			// in the history.
			rh->impl.push_back(rhe);
			ret = true;
		}
	} catch (std::exception const &e) {
		U_LOG_E("Caught exception: %s", e.what());
	}
	return ret;
}

enum m_relation_history_result
m_relation_history_get(struct m_relation_history *rh, uint64_t at_timestamp_ns, struct xrt_space_relation *out_relation)
{
	XRT_TRACE_MARKER();
	std::unique_lock<os::Mutex> lock(rh->mutex);
	try {
		if (rh->impl.empty() || at_timestamp_ns == 0) {
			// Do nothing. You push nothing to the buffer you get nothing from the buffer.
			*out_relation = {};
			return M_RELATION_HISTORY_RESULT_INVALID;
		}
		const auto b = rh->impl.begin();
		const auto e = rh->impl.end();

		// find the first element *not less than* our value. the lambda we pass is the comparison
		// function, to compare against timestamps.
		const auto it =
		    std::lower_bound(b, e, at_timestamp_ns, [](const relation_history_entry &rhe, uint64_t timestamp) {
			    return rhe.timestamp < timestamp;
		    });

		if (it == e) {
			// lower bound is at the end:
			// The desired timestamp is after what our buffer contains.
			// (pose-prediction)
			int64_t diff_prediction_ns = static_cast<int64_t>(at_timestamp_ns) - rh->impl.back().timestamp;
			double delta_s = time_ns_to_s(diff_prediction_ns);

			U_LOG_T("Extrapolating %f s past the back of the buffer!", delta_s);

			m_predict_relation(&rh->impl.back().relation, delta_s, out_relation);
			return M_RELATION_HISTORY_RESULT_PREDICTED;
		}
		if (at_timestamp_ns == it->timestamp) {
			// exact match
			U_LOG_T("Exact match in the buffer!");
			*out_relation = it->relation;
			return M_RELATION_HISTORY_RESULT_EXACT;
		}
		if (it == b) {
			// lower bound is at the beginning (and it's not an exact match):
			// The desired timestamp is before what our buffer contains.
			// (an edge case where somebody asks for a really old pose and we do our best)
			int64_t diff_prediction_ns = static_cast<int64_t>(at_timestamp_ns) - rh->impl.front().timestamp;
			double delta_s = time_ns_to_s(diff_prediction_ns);
			U_LOG_T("Extrapolating %f s before the front of the buffer!", delta_s);
			m_predict_relation(&rh->impl.front().relation, delta_s, out_relation);
			return M_RELATION_HISTORY_RESULT_REVERSE_PREDICTED;
		}
		U_LOG_T("Interpolating within buffer!");

		// We precede *it and follow *(it - 1) (which we know exists because we already handled
		// the it = begin() case)
		const auto &predecessor = *(it - 1);
		const auto &successor = *it;

		// Do the thing.
		int64_t diff_before = static_cast<int64_t>(at_timestamp_ns) - predecessor.timestamp;
		int64_t diff_after = static_cast<int64_t>(successor.timestamp) - at_timestamp_ns;

		float amount_to_lerp = (float)diff_before / (float)(diff_before + diff_after);

		// Copy relation flags
		xrt_space_relation result{};
		result.relation_flags = (enum xrt_space_relation_flags)(predecessor.relation.relation_flags &
		                                                        successor.relation.relation_flags);
		// First-order implementation - lerp between the before and after
		if (0 != (result.relation_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT)) {
			result.pose.position = m_vec3_lerp(predecessor.relation.pose.position,
			                                   successor.relation.pose.position, amount_to_lerp);
		}
		if (0 != (result.relation_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT)) {

			math_quat_slerp(&predecessor.relation.pose.orientation, &successor.relation.pose.orientation,
			                amount_to_lerp, &result.pose.orientation);
		}

		//! @todo Does interpolating the velocities make any sense?
		if (0 != (result.relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT)) {
			result.angular_velocity = m_vec3_lerp(predecessor.relation.angular_velocity,
			                                      successor.relation.angular_velocity, amount_to_lerp);
		}
		if (0 != (result.relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT)) {
			result.linear_velocity = m_vec3_lerp(predecessor.relation.linear_velocity,
			                                     successor.relation.linear_velocity, amount_to_lerp);
		}
		*out_relation = result;
		return M_RELATION_HISTORY_RESULT_INTERPOLATED;

	} catch (std::exception const &e) {
		U_LOG_E("Caught exception: %s", e.what());
		return M_RELATION_HISTORY_RESULT_INVALID;
	}
}

bool
m_relation_history_estimate_motion(struct m_relation_history *rh,
                                   const struct xrt_space_relation *in_relation,
                                   uint64_t timestamp,
                                   struct xrt_space_relation *out_relation)
{

	uint64_t last_time_ns;
	struct xrt_space_relation last_relation;
	if (!m_relation_history_get_latest(rh, &last_time_ns, &last_relation)) {
		return false;
	};

	float dt = (float)time_ns_to_s(timestamp - last_time_ns);

	// Used to find out what values are valid in both the old relation and the new relation
	enum xrt_space_relation_flags tmp_flags =
	    (enum xrt_space_relation_flags)(last_relation.relation_flags & in_relation->relation_flags);

	// Brevity
	enum xrt_space_relation_flags &outf = out_relation->relation_flags;


	if (tmp_flags & XRT_SPACE_RELATION_POSITION_VALID_BIT) {
		outf = (enum xrt_space_relation_flags)(outf | XRT_SPACE_RELATION_POSITION_VALID_BIT);
		outf = (enum xrt_space_relation_flags)(outf | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

		outf = (enum xrt_space_relation_flags)(outf | XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT);

		out_relation->linear_velocity = (in_relation->pose.position - last_relation.pose.position) / dt;
	}

	if (tmp_flags & XRT_SPACE_RELATION_ORIENTATION_VALID_BIT) {
		outf = (enum xrt_space_relation_flags)(outf | XRT_SPACE_RELATION_ORIENTATION_VALID_BIT);
		outf = (enum xrt_space_relation_flags)(outf | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

		outf = (enum xrt_space_relation_flags)(outf | XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT);

		math_quat_finite_difference(&last_relation.pose.orientation, &in_relation->pose.orientation, dt,
		                            &out_relation->angular_velocity);
	}

	out_relation->pose = in_relation->pose;

	return true;
}

bool
m_relation_history_get_latest(struct m_relation_history *rh,
                              uint64_t *out_time_ns,
                              struct xrt_space_relation *out_relation)
{
	std::unique_lock<os::Mutex> lock(rh->mutex);
	if (rh->impl.empty()) {
		return false;
	}
	*out_relation = rh->impl.back().relation;
	*out_time_ns = rh->impl.back().timestamp;
	return true;
}

uint32_t
m_relation_history_get_size(const struct m_relation_history *rh)
{
	return (uint32_t)rh->impl.size();
}

void
m_relation_history_clear(struct m_relation_history *rh)
{
	std::unique_lock<os::Mutex> lock(rh->mutex);
	rh->impl.clear();
}

void
m_relation_history_destroy(struct m_relation_history **rh_ptr)
{
	struct m_relation_history *rh = *rh_ptr;
	if (rh == NULL) {
		// Do nothing, it's likely already been destroyed
		return;
	}
	try {
		delete rh;
	} catch (std::exception const &e) {
		U_LOG_E("Caught exception: %s", e.what());
	}
	*rh_ptr = NULL;
}
