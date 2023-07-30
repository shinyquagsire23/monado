// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR tracking source.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_tracking
 */

#include "t_openvr_tracker.h"

#include "util/u_logging.h"
#include "xrt/xrt_config_have.h"

#ifdef XRT_HAVE_OPENVR
#include <openvr.h>
#include <cstddef>
#include <map>

#include "math/m_api.h"
#include "os/os_threading.h"
#include "util/u_logging.h"
#include "util/u_time.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_tracking.h"

struct openvr_tracker
{
	vr::IVRSystem *vr_system;
	struct os_thread_helper thread;
	std::map<enum openvr_device, struct xrt_pose_sink *> sinks;
	double sample_frequency_hz;

	struct xrt_pose_sink *
	get_sink_for_device_index(uint64_t i)
	{
		vr::ETrackedDeviceClass dev_class = vr_system->GetTrackedDeviceClass(i);
		struct xrt_pose_sink *sink = nullptr;
		if (dev_class == vr::TrackedDeviceClass_HMD && sinks.count(T_OPENVR_DEVICE_HMD) > 0) {
			sink = sinks.at(T_OPENVR_DEVICE_HMD);
		} else if (dev_class == vr::TrackedDeviceClass_Controller &&
		           vr_system->GetControllerRoleForTrackedDeviceIndex(i) == vr::TrackedControllerRole_LeftHand &&
		           sinks.count(T_OPENVR_DEVICE_LEFT_CONTROLLER) > 0) {
			sink = sinks.at(T_OPENVR_DEVICE_LEFT_CONTROLLER);
		} else if (dev_class == vr::TrackedDeviceClass_Controller &&
		           vr_system->GetControllerRoleForTrackedDeviceIndex(i) ==
		               vr::TrackedControllerRole_RightHand &&
		           sinks.count(T_OPENVR_DEVICE_RIGHT_CONTROLLER) > 0) {
			sink = sinks.at(T_OPENVR_DEVICE_RIGHT_CONTROLLER);
		} else if (dev_class == vr::TrackedDeviceClass_GenericTracker &&
		           sinks.count(T_OPENVR_DEVICE_TRACKER) > 0) {
			sink = sinks.at(T_OPENVR_DEVICE_TRACKER);
		}
		return sink;
	}
};

static void *
tracking_loop(void *ot_ptr)
{
	struct openvr_tracker *ovrt = (struct openvr_tracker *)ot_ptr;

	while (os_thread_helper_is_running(&ovrt->thread)) {
		os_nanosleep(U_TIME_1S_IN_NS / ovrt->sample_frequency_hz);

		// Flush events
		vr::VREvent_t event;
		while (ovrt->vr_system->PollNextEvent(&event, sizeof(event))) {
		}

		timepoint_ns now = os_monotonic_get_ns();

		const uint32_t MAX_DEVS = vr::k_unMaxTrackedDeviceCount;
		auto origin = vr::ETrackingUniverseOrigin::TrackingUniverseRawAndUncalibrated;
		vr::TrackedDevicePose_t poses[MAX_DEVS];
		ovrt->vr_system->GetDeviceToAbsoluteTrackingPose(origin, 0, poses, MAX_DEVS);

		for (uint32_t i = 0; i < MAX_DEVS; i++) {
			struct xrt_pose_sink *sink_for_i = ovrt->get_sink_for_device_index(i);
			if (sink_for_i != nullptr && poses[i].bDeviceIsConnected && poses[i].bPoseIsValid) {
				const auto &m = poses[i].mDeviceToAbsoluteTracking.m;
				struct xrt_vec3 p = {m[0][3], m[1][3], m[2][3]};
				struct xrt_matrix_3x3 R = {
				    m[0][0], m[0][1], m[0][2], //
				    m[1][0], m[1][1], m[1][2], //
				    m[2][0], m[2][1], m[2][2], //
				};
				struct xrt_quat q = {};
				math_quat_from_matrix_3x3(&R, &q);

				struct xrt_pose_sample sample = {now, {q, p}};
				xrt_sink_push_pose(sink_for_i, &sample);
			}
		}
	}

	return nullptr;
}

extern "C" {

struct openvr_tracker *
t_openvr_tracker_create(double sample_frequency, enum openvr_device *devs, struct xrt_pose_sink **sinks, int sink_count)
{
	struct openvr_tracker *ovrt = new openvr_tracker{};
	os_thread_helper_init(&ovrt->thread);

	for (int i = 0; i < sink_count; i++) {
		ovrt->sinks[devs[i]] = sinks[i];
	}
	ovrt->sample_frequency_hz = sample_frequency;

	vr::EVRInitError e = vr::VRInitError_None;
	ovrt->vr_system = vr::VR_Init(&e, vr::VRApplication_Background);
	if (e != vr::VRInitError_None) {
		if (e == vr::VRInitError_Init_NoServerForBackgroundApp) {
			U_LOG_E("Unable to find OpenVR server running. error=%d", e);
		} else {
			U_LOG_E("Unable to initialize OpenVR, error=%d", e);
		}
		return nullptr;
	}
	U_LOG(U_LOGGING_INFO, "OpenVR tracker created");
	return ovrt;
}

void
t_openvr_tracker_start(struct openvr_tracker *ovrt)
{
	os_thread_helper_start(&ovrt->thread, tracking_loop, ovrt);
}

void
t_openvr_tracker_stop(struct openvr_tracker *ovrt)
{
	os_thread_helper_stop_and_wait(&ovrt->thread);
}

void
t_openvr_tracker_destroy(struct openvr_tracker *ovrt)
{
	if (os_thread_helper_is_running(&ovrt->thread)) {
		t_openvr_tracker_stop(ovrt);
	}
	vr::VR_Shutdown();
	ovrt->vr_system = nullptr;
	os_thread_helper_destroy(&ovrt->thread);
	delete ovrt;
}
}

#else

struct openvr_tracker *
t_openvr_tracker_create(double /*unused*/,
                        enum openvr_device * /*unused*/,
                        struct xrt_pose_sink ** /*unused*/,
                        int /*unused*/)
{
	U_LOG_W("OpenVR was not built, unable to initialize lighthouse tracking.");
	return nullptr;
}

void
t_openvr_tracker_start(struct openvr_tracker * /*unused*/)
{}

void
t_openvr_tracker_stop(struct openvr_tracker * /*unused*/)
{}

void
t_openvr_tracker_destroy(struct openvr_tracker * /*unused*/)
{}
#endif
