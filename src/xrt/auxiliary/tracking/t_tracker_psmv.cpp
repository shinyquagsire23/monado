// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PS Move tracker code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_tracking.h"

#include "tracking/t_tracking.h"

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_frame.h"
#include "util/u_format.h"

#include "math/m_api.h"

#include "os/os_threading.h"

#include <stdio.h>
#include <assert.h>
#include <pthread.h>


class TrackerPSMV
{
public:
	struct xrt_tracked_psmv base = {};
	struct xrt_frame_sink sink = {};
	struct xrt_frame_node node = {};

	//! Frame waiting to be processed.
	struct xrt_frame *frame;

	//! Thread and lock helper.
	struct os_thread_helper oth;

	//! Have we received a new IMU sample.
	bool has_imu = false;

	struct
	{
		struct xrt_vec3 pos = {};
		struct xrt_quat rot = {};
	} fusion;
};

static void
procces(TrackerPSMV &t, struct xrt_frame *xf)
{
	// Only IMU data
	if (xf == NULL) {
		return;
	}

	xrt_frame_reference(&xf, NULL);
}

static void
run(TrackerPSMV &t)
{
	struct xrt_frame *frame = NULL;

	os_thread_helper_lock(&t.oth);

	while (os_thread_helper_is_running_locked(&t.oth)) {
		// No data
		if (!t.has_imu || t.frame == NULL) {
			os_thread_helper_wait_locked(&t.oth);
		}

		if (!os_thread_helper_is_running_locked(&t.oth)) {
			break;
		}

		// Take a reference on the current frame, this keeps it alive
		// if it is replaced during the consumer processing it, but
		// we no longer need to hold onto the frame on the queue we
		// just move the pointer.
		frame = t.frame;
		t.frame = NULL;

		// Unlock the mutex when we do the work.
		os_thread_helper_unlock(&t.oth);

		procces(t, frame);

		// Have to lock it again.
		os_thread_helper_lock(&t.oth);
	}

	os_thread_helper_unlock(&t.oth);
}

static void
get_pose(TrackerPSMV &t,
         struct time_state *timestate,
         timepoint_ns when_ns,
         struct xrt_space_relation *out_relation)
{
	os_thread_helper_lock(&t.oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&t.oth)) {
		os_thread_helper_unlock(&t.oth);
		return;
	}

	out_relation->pose.position = t.fusion.pos;
	out_relation->pose.orientation = t.fusion.rot;

	//! @todo assuming that orientation is actually currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_POSITION_TRACKED_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

	os_thread_helper_unlock(&t.oth);
}

static void
imu_data(TrackerPSMV &t,
         time_duration_ns delta_ns,
         struct xrt_tracking_sample *sample)
{
	os_thread_helper_lock(&t.oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&t.oth)) {
		os_thread_helper_unlock(&t.oth);
		return;
	}

	float dt = time_ns_to_s(delta_ns);
	// Super simple fusion.
	math_quat_integrate_velocity(&t.fusion.rot, &sample->gyro_rad_secs, dt,
	                             &t.fusion.rot);

	os_thread_helper_unlock(&t.oth);
}

static void
frame(TrackerPSMV &t, struct xrt_frame *xf)
{
	os_thread_helper_lock(&t.oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&t.oth)) {
		os_thread_helper_unlock(&t.oth);
		return;
	}

	xrt_frame_reference(&t.frame, xf);

	// Wake up the thread.
	os_thread_helper_signal_locked(&t.oth);

	os_thread_helper_unlock(&t.oth);
}

static void
break_apart(TrackerPSMV &t)
{
	os_thread_helper_stop(&t.oth);
}


/*
 *
 * C wrapper functions.
 *
 */

extern "C" void
t_psmv_push_imu(struct xrt_tracked_psmv *xtmv,
                time_duration_ns delta_ns,
                struct xrt_tracking_sample *sample)
{
	auto &t = *container_of(xtmv, TrackerPSMV, base);
	imu_data(t, delta_ns, sample);
}

extern "C" void
t_psmv_get_tracked_pose(struct xrt_tracked_psmv *xtmv,
                        struct time_state *timestate,
                        timepoint_ns when_ns,
                        struct xrt_space_relation *out_relation)
{
	auto &t = *container_of(xtmv, TrackerPSMV, base);
	get_pose(t, timestate, when_ns, out_relation);
}

extern "C" void
t_psmv_fake_destroy(struct xrt_tracked_psmv *xtmv)
{
	auto &t = *container_of(xtmv, TrackerPSMV, base);
	(void)t;
	// Not the real destroy function
}

extern "C" void
t_psmv_sink_push_frame(struct xrt_frame_sink *xsink, struct xrt_frame *xf)
{
	auto &t = *container_of(xsink, TrackerPSMV, sink);
	frame(t, xf);
}

extern "C" void
t_psmv_node_break_apart(struct xrt_frame_node *node)
{
	auto &t = *container_of(node, TrackerPSMV, node);
	break_apart(t);
}

extern "C" void
t_psmv_node_destroy(struct xrt_frame_node *node)
{
	auto t_ptr = container_of(node, TrackerPSMV, node);

	os_thread_helper_destroy(&t_ptr->oth);

	delete t_ptr;
}

extern "C" void *
t_psmv_run(void *ptr)
{
	auto &t = *(TrackerPSMV *)ptr;
	run(t);
	return NULL;
}


/*
 *
 * Exported functions.
 *
 */

extern "C" int
t_psmv_start(struct xrt_tracked_psmv *xtmv)
{
	auto &t = *container_of(xtmv, TrackerPSMV, base);
	int ret;

	ret = os_thread_helper_start(&t.oth, t_psmv_run, &t);
	if (ret != 0) {
		return ret;
	}

	return ret;
}

extern "C" int
t_psmv_create(struct xrt_frame_context *xfctx,
              struct xrt_colour_rgb_f32 *rgb,
              struct xrt_tracked_psmv **out_xtmv,
              struct xrt_frame_sink **out_sink)
{
	fprintf(stderr, "%s\n", __func__);

	auto &t = *(new TrackerPSMV());
	int ret;

	t.base.get_tracked_pose = t_psmv_get_tracked_pose;
	t.base.push_imu = t_psmv_push_imu;
	t.base.destroy = t_psmv_fake_destroy;
	t.base.colour = *rgb;
	t.sink.push_frame = t_psmv_sink_push_frame;
	t.node.break_apart = t_psmv_node_break_apart;
	t.node.destroy = t_psmv_node_destroy;
	t.fusion.rot.x = 0.0f;
	t.fusion.rot.y = 0.0f;
	t.fusion.rot.z = 0.0f;
	t.fusion.rot.w = 1.0f;

	ret = os_thread_helper_init(&t.oth);
	if (ret != 0) {
		delete (&t);
		return ret;
	}

	static int hack = 0;
	switch (hack++) {
	case 0:
		t.fusion.pos.x = -0.3f;
		t.fusion.pos.y = 1.3f;
		t.fusion.pos.z = -0.5f;
		break;
	case 1:
		t.fusion.pos.x = 0.3f;
		t.fusion.pos.y = 1.3f;
		t.fusion.pos.z = -0.5f;
		break;
	default:
		t.fusion.pos.x = 0.0f;
		t.fusion.pos.y = 0.8f + hack * 0.1f;
		t.fusion.pos.z = -0.5f;
		break;
	}

	xrt_frame_context_add(xfctx, &t.node);

	*out_sink = &t.sink;
	*out_xtmv = &t.base;

	return 0;
}
