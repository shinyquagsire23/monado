// Copyright 2022-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface for vive data sources
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_vive
 */

#include "xrt/xrt_frame.h"
#include "xrt/xrt_tracking.h"

#include "os/os_threading.h"

#include "math/m_clock_offset.h"

#include "util/u_deque.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"

#include "vive.h"


/*!
 * Manages the data streaming state related to a vive headset.
 *
 * @implements xrt_frame_node
 */
struct vive_source
{
	struct xrt_frame_node node;
	enum u_logging_level log_level;

	// Sinks
	struct xrt_frame_sink sbs_sink;  //!< Intermediate sink for SBS frames
	struct xrt_imu_sink imu_sink;    //!< Intermediate sink for IMU samples
	struct xrt_slam_sinks in_sinks;  //!< Pointers to intermediate sinks
	struct xrt_slam_sinks out_sinks; //!< Pointers to downstream sinks

	// V4L2 frame streaming state
	bool timestamps_have_been_zero_until_now; //!< First v4l2 frames are zeroed
	bool waiting_for_first_nonempty_frame;    //!< Whether the first good frame has been received

	// Frame timestamps
	struct u_deque_timepoint_ns frame_timestamps; //! Queue of yet unused frame hw timestamps
	struct os_mutex frame_timestamps_lock;        //! Lock for accessing frame_timestamps
	uint32_t last_frame_ticks;                    //! Last frame timestamp in device ticks
	timepoint_ns last_frame_ts_ns;                //! Last frame timestamp in device nanoseconds

	// Clock offsets
	time_duration_ns hw2mono; //!< Estimated offset from IMU to monotonic clock
	time_duration_ns hw2v4l2; //!< Estimated offset from IMU to V4L2 clock
};

/*
 *
 * Vive source methods
 *
 */

//! Find the best corresponding hw timestamp from this v4l2 frame, return
//! whether it was found.
bool
vive_source_try_convert_v4l2_timestamp(struct vive_source *vs, struct xrt_frame *xf)
{
	assert(xf->timestamp != 0 || vs->timestamps_have_been_zero_until_now);
	if (xf->timestamp == 0) {
		return false;
	}
	vs->timestamps_have_been_zero_until_now = false;

	struct u_deque_timepoint_ns vive_timestamps = vs->frame_timestamps;
	struct os_mutex *vive_timestamps_lock = &vs->frame_timestamps_lock;

	timepoint_ns v4l2_ts = xf->timestamp;

	size_t vive_ts_count = u_deque_timepoint_ns_size(vive_timestamps);
	if (vive_ts_count == 0) { // This seems to happen in some runs
		// This code assumes vive_timestamps will always be populated before v4l2
		// receives a frame, thus if we reach this, this assumption has failed.
		// As a fallback we'll use the v4l2 timestamp corrected to monotonic clock.
		VIVE_TRACE(vs, "No vive timestamps available for this v4l2 frame, will use v4l2 timestamp");
		timepoint_ns hw_ts = v4l2_ts - vs->hw2v4l2;
		xf->timestamp = hw_ts + vs->hw2mono;
		return true;
	}

	os_mutex_lock(vive_timestamps_lock);

	// Find i in vive_timestamps that would be closer to xf->timestamp in v4l2 clock
	int closer_i = -1;
	timepoint_ns vive_ts = -1;
	time_duration_ns min_distance = INT64_MAX;
	for (size_t i = 0; i < vive_ts_count; i++) {
		vive_ts = u_deque_timepoint_ns_at(vive_timestamps, i);
		timepoint_ns v4l2_ts_est = vive_ts + vs->hw2v4l2;
		time_duration_ns distance = llabs(v4l2_ts_est - v4l2_ts);
		if (distance < min_distance) {
			closer_i = i;
			min_distance = distance;
		}
	}

	// Discard missed frames and set vive_timestamp to use in this frame
	timepoint_ns vive_timestamp = 0;
	for (; closer_i >= 0; closer_i--) {
		u_deque_timepoint_ns_pop_front(vive_timestamps, &vive_timestamp);
	}

	os_mutex_unlock(vive_timestamps_lock);

	// Our estimate is within a reasonable time distance
	assert(min_distance < U_TIME_1S_IN_NS / CAMERA_FREQUENCY || vs->waiting_for_first_nonempty_frame);
	vs->waiting_for_first_nonempty_frame = false;

	// Update estimate of hw2v4l2 clock offset, only used for matching timestamps
	m_clock_offset_a2b(CAMERA_FREQUENCY, vive_timestamp, xf->timestamp, &vs->hw2v4l2);

	// Use vive_timestamp and put it in monotonic clock
	xf->timestamp = vive_timestamp + vs->hw2mono; // Notice that we don't use hw2v4l2

	return true;
}

static void
vive_source_receive_sbs_frame(struct xrt_frame_sink *sink, struct xrt_frame *xf)
{
	struct vive_source *vs = container_of(sink, struct vive_source, sbs_sink);
	bool should_push = vive_source_try_convert_v4l2_timestamp(vs, xf);

	if (!should_push) {
		VIVE_TRACE(vs, "skipped sbs img t=%ld source_t=%ld", xf->timestamp, xf->source_timestamp);
		return;
	}

	VIVE_TRACE(vs, "sbs img t=%ld source_t=%ld", xf->timestamp, xf->source_timestamp);

	if (vs->out_sinks.cams[0]) { // The split into left right will happen downstream
		xrt_sink_push_frame(vs->out_sinks.cams[0], xf);
	}
}

static void
vive_source_receive_imu_sample(struct xrt_imu_sink *sink, struct xrt_imu_sample *s)
{
	struct vive_source *vs = container_of(sink, struct vive_source, imu_sink);

	timepoint_ns ts = s->timestamp_ns;
	struct xrt_vec3_f64 a = s->accel_m_s2;
	struct xrt_vec3_f64 w = s->gyro_rad_secs;
	VIVE_TRACE(vs, "imu t=%ld a=(%f %f %f) w=(%f %f %f)", ts, a.x, a.y, a.z, w.x, w.y, w.z);

	if (vs->out_sinks.imu) {
		xrt_sink_push_imu(vs->out_sinks.imu, s);
	}
}

static void
vive_source_node_break_apart(struct xrt_frame_node *node)
{}

static void
vive_source_node_destroy(struct xrt_frame_node *node)
{
	struct vive_source *vs = container_of(node, struct vive_source, node);
	os_mutex_destroy(&vs->frame_timestamps_lock);
	u_deque_timepoint_ns_destroy(&vs->frame_timestamps);

	free(vs);
}


/*!
 *
 * Exported functions
 *
 */

struct vive_source *
vive_source_create(struct xrt_frame_context *xfctx)
{
	struct vive_source *vs = U_TYPED_CALLOC(struct vive_source);
	vs->log_level = debug_get_log_option_vive_log();

	// Setup sinks
	vs->sbs_sink.push_frame = vive_source_receive_sbs_frame;
	vs->imu_sink.push_imu = vive_source_receive_imu_sample;
	vs->in_sinks.cam_count = 1;
	vs->in_sinks.cams[0] = &vs->sbs_sink;
	vs->in_sinks.imu = &vs->imu_sink;

	vs->timestamps_have_been_zero_until_now = true;
	vs->waiting_for_first_nonempty_frame = true;

	vs->frame_timestamps = u_deque_timepoint_ns_create();
	os_mutex_init(&vs->frame_timestamps_lock);

	// Setup node
	struct xrt_frame_node *xfn = &vs->node;
	xfn->break_apart = vive_source_node_break_apart;
	xfn->destroy = vive_source_node_destroy;
	xrt_frame_context_add(xfctx, &vs->node);

	VIVE_DEBUG(vs, "Vive source created");

	return vs;
}

void
vive_source_push_imu_packet(struct vive_source *vs, uint32_t age, timepoint_ns t, struct xrt_vec3 a, struct xrt_vec3 g)
{
	/*
	 * We want the samples to be on sometime in the past, not future. This
	 * is due to USB latency, which we don't know, so we are guessing here.
	 * We also don't know if the timestamp given for the start of the sample
	 * or the end.
	 *
	 * We picked 2 here because that's about what the best gaming mice can
	 * do, it also seems to feel good with what seems to be reasonable
	 * present to display offset in the compositor.
	 *
	 * We also adjust for the "age" of a sample, the vive sends out 3
	 * samples per packet, most often only one is a new sample. But
	 * sometimes we get up to 3 new samples in one packet. So if age is
	 * greater then 0, adjust with that many MS (1000Hz sampler rate).
	 */

	// 2 ms in value.
	const timepoint_ns t2ms_ns = U_TIME_1MS_IN_NS * 2;

	// Extra in the past for age.
	timepoint_ns age_diff_ns = age * U_TIME_1MS_IN_NS;

	// Now.
	timepoint_ns now_ns = (timepoint_ns)os_monotonic_get_ns();

	// Calculated sample point.
	timepoint_ns sample_point = now_ns - t2ms_ns - age_diff_ns;

	// Time adjustment.
	t = m_clock_offset_a2b(IMU_FREQUENCY, t, sample_point, &vs->hw2mono);

	// Finished sample.
	struct xrt_imu_sample sample = {
	    .timestamp_ns = t,
	    .accel_m_s2 = (struct xrt_vec3_f64){a.x, a.y, a.z},
	    .gyro_rad_secs = (struct xrt_vec3_f64){g.x, g.y, g.z},
	};

	// Push it out!
	xrt_sink_push_imu(&vs->imu_sink, &sample);

	// Only do this if we are really debugging stuff.
#ifdef XRT_FEATURE_TRACING
	timepoint_ns diff_ns = t - (now_ns - age_diff_ns);
	static timepoint_ns last_ns = 0;
	if (last_ns == 0) {
		last_ns = t;
	}

	double now_diff_ms = time_ns_to_ms_f(diff_ns);
	double last_diff_ms = time_ns_to_ms_f(t - last_ns);
	last_ns = t;

#ifdef U_TRACE_TRACY
	TracyCPlot("Vive IMU to now(ms)", now_diff_ms);
	TracyCPlot("Vive IMU to last(ms)", last_diff_ms);
	TracyCPlot("Vive IMU age", age);
#endif

	VIVE_TRACE(vs, "Sample diffs, now: %+.4fms, last: %+.4f, age: %u", now_diff_ms, last_diff_ms, age);
#endif
}

void
vive_source_push_frame_ticks(struct vive_source *vs, timepoint_ns ticks)
{
	ticks_to_ns(ticks, &vs->last_frame_ticks, &vs->last_frame_ts_ns);
	u_deque_timepoint_ns_push_back(vs->frame_timestamps, vs->last_frame_ts_ns);
}

void
vive_source_hook_into_sinks(struct vive_source *vs, struct xrt_slam_sinks *sinks)
{
	vs->out_sinks = *sinks;
	sinks->cam_count = 1;
	sinks->cams[0] = vs->in_sinks.cams[0];
}
