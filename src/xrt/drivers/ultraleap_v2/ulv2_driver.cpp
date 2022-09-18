// Copyright 2020-2021, Collabora, Ltd.
// Copyright 2020-2021, Moses Turner
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief   Driver for Ultraleap's V2 API for the Leap Motion Controller.
 * @author  Moses Turner <mosesturner@protonmail.com>
 * @author  Christoph Haag <christoph.haag@collabora.com>
 * @ingroup drv_ulv2
 */

#include "ulv2_interface.h"
#include "util/u_device.h"
#include "util/u_var.h"
#include "util/u_debug.h"
#include "math/m_space.h"
#include "math/m_api.h"
#include "util/u_time.h"
#include "os/os_time.h"
#include "os/os_threading.h"

#include "Leap.h"

DEBUG_GET_ONCE_LOG_OPTION(ulv2_log, "ULV2_LOG", U_LOGGING_INFO)

#define ULV2_TRACE(ulv2d, ...) U_LOG_XDEV_IFL_T(&ulv2d->base, ulv2d->log_level, __VA_ARGS__)
#define ULV2_DEBUG(ulv2d, ...) U_LOG_XDEV_IFL_D(&ulv2d->base, ulv2d->log_level, __VA_ARGS__)
#define ULV2_INFO(ulv2d, ...) U_LOG_XDEV_IFL_I(&ulv2d->base, ulv2d->log_level, __VA_ARGS__)
#define ULV2_WARN(ulv2d, ...) U_LOG_XDEV_IFL_W(&ulv2d->base, ulv2d->log_level, __VA_ARGS__)
#define ULV2_ERROR(ulv2d, ...) U_LOG_XDEV_IFL_E(&ulv2d->base, ulv2d->log_level, __VA_ARGS__)

#define printf_pose(pose)                                                                                              \
	printf("%f %f %f  %f %f %f %f\n", pose.position.x, pose.position.y, pose.position.z, pose.orientation.x,       \
	       pose.orientation.y, pose.orientation.z, pose.orientation.w);
extern "C" {

enum xrt_space_relation_flags valid_flags = (enum xrt_space_relation_flags)(
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

// Excuse my sketchy thread stuff, I'm sure this violates all kinds of best practices. It uusssuallyyy doesn't explode.
// -Moses Turner
enum leap_thread_status
{
	THREAD_NOT_STARTED,
	THREAD_OK,
	THREAD_ERRORED_OUT,
};

struct ulv2_device
{
	struct xrt_device base;

	struct xrt_tracking_origin tracking_origin;

	enum u_logging_level log_level;

	bool pthread_should_stop;

	enum leap_thread_status our_thread_status;

	struct os_thread_helper leap_loop_oth;

	struct xrt_hand_joint_set joints_write_in[2];

	// Only read/write these last two if you have the mutex
	struct xrt_hand_joint_set joints_read_out[2];

	bool hand_exists[2];
};

inline struct ulv2_device *
ulv2_device(struct xrt_device *xdev)
{
	return (struct ulv2_device *)xdev;
}


// Roughly, take the Leap hand joint, do some coordinate conversions, and save it in a xrt_hand_joint_value
static void
ulv2_process_joint(
    Leap::Vector jointpos, Leap::Matrix jointbasis, float width, int side, struct xrt_hand_joint_value *joint)
{
	struct xrt_space_relation *relation = &joint->relation;
	joint->radius = (width / 1000) / 2;

	struct xrt_matrix_3x3 turn_into_quat;
	// clang-format off
	// These are matrices so we want to preserve where the rows and columns are, hence the clang-format off
	if (side == 1)
	{
		turn_into_quat = {-jointbasis.xBasis.x, -jointbasis.yBasis.x, -jointbasis.zBasis.x,
		                  -jointbasis.xBasis.z, -jointbasis.yBasis.z, -jointbasis.zBasis.z,
		                  -jointbasis.xBasis.y, -jointbasis.yBasis.y, -jointbasis.zBasis.y};
	}
	else
	{
		turn_into_quat = {jointbasis.xBasis.x,  -jointbasis.yBasis.x, -jointbasis.zBasis.x,
		                  jointbasis.xBasis.z,  -jointbasis.yBasis.z, -jointbasis.zBasis.z,
		                  jointbasis.xBasis.y,  -jointbasis.yBasis.y, -jointbasis.zBasis.y};
	}
	// clang-format on


	math_quat_from_matrix_3x3(&turn_into_quat, &relation->pose.orientation);
	relation->pose.position.x = jointpos.x * -1 / 1000.0;
	relation->pose.position.y = jointpos.z * -1 / 1000.0;
	relation->pose.position.z = jointpos.y * -1 / 1000.0;
	relation->relation_flags = valid_flags;
}

static int error_time; // Lazy counter to prevent printing error messages at 120hz


void
ulv2_process_hand(Leap::Hand hand, xrt_hand_joint_set *joint_set, int hi)
{

#define xrtj(y) &joint_set->values.hand_joint_set_default[XRT_HAND_JOINT_##y]

	ulv2_process_joint(hand.palmPosition(), hand.basis(), 50, hi, xrtj(PALM));
	ulv2_process_joint(hand.wristPosition(), hand.arm().basis(), 50, hi, xrtj(WRIST));

	const Leap::FingerList fingers = hand.fingers();

	// Bunch of macros to make the following
	// boilerplate easier to deal with

#define fb(y) finger.bone(y)
#define prevJ(y) finger.bone(y).prevJoint()
#define nextJ(y) finger.bone(y).nextJoint()

#define lm Leap::Bone::TYPE_METACARPAL
#define lp Leap::Bone::TYPE_PROXIMAL
#define li Leap::Bone::TYPE_INTERMEDIATE
#define ld Leap::Bone::TYPE_DISTAL

	for (Leap::FingerList::const_iterator fl = fingers.begin(); fl != fingers.end(); ++fl) {
		// Iterate on the list of fingers Leap gives us
		const Leap::Finger finger = *fl;
		Leap::Finger::Type fingerType = finger.type();
		switch (fingerType) {
		case Leap::Finger::Type::TYPE_THUMB:
			// If the finger is a thumb, then for each joint: process the Leap joint location,
			// rotation matrix, finger width, hand side (0 if left, 1 if right),
			// and write the finger radius and powe out to the correct xrt_hand_joint_value.
			ulv2_process_joint(prevJ(lp), fb(lp).basis(), fb(lp).width(), hi, xrtj(THUMB_METACARPAL));
			ulv2_process_joint(prevJ(li), fb(li).basis(), fb(lp).width(), hi, xrtj(THUMB_PROXIMAL));
			ulv2_process_joint(prevJ(ld), fb(ld).basis(), fb(li).width(), hi, xrtj(THUMB_DISTAL));
			ulv2_process_joint(nextJ(ld), fb(ld).basis(), fb(ld).width(), hi, xrtj(THUMB_TIP));
			// Note that there are only four joints here as opposed to all the other fingers which have five
			// joints.
			break;
		case Leap::Finger::Type::TYPE_INDEX:
			// If the finger is an index finger, do the same thing but with index joints
			ulv2_process_joint(prevJ(lm), fb(lm).basis(), fb(lm).width(), hi, xrtj(INDEX_METACARPAL));
			ulv2_process_joint(prevJ(lp), fb(lp).basis(), fb(lm).width(), hi, xrtj(INDEX_PROXIMAL));
			ulv2_process_joint(prevJ(li), fb(li).basis(), fb(lp).width(), hi, xrtj(INDEX_INTERMEDIATE));
			ulv2_process_joint(prevJ(ld), fb(ld).basis(), fb(li).width(), hi, xrtj(INDEX_DISTAL));
			ulv2_process_joint(nextJ(ld), fb(ld).basis(), fb(ld).width(), hi, xrtj(INDEX_TIP));
			break;
		case Leap::Finger::Type::TYPE_MIDDLE:
			// If the finger is a middle finger, do the same thing but with middle joints
			ulv2_process_joint(prevJ(lm), fb(lm).basis(), fb(lm).width(), hi, xrtj(MIDDLE_METACARPAL));
			ulv2_process_joint(prevJ(lp), fb(lp).basis(), fb(lm).width(), hi, xrtj(MIDDLE_PROXIMAL));
			ulv2_process_joint(prevJ(li), fb(li).basis(), fb(lp).width(), hi, xrtj(MIDDLE_INTERMEDIATE));
			ulv2_process_joint(prevJ(ld), fb(ld).basis(), fb(li).width(), hi, xrtj(MIDDLE_DISTAL));
			ulv2_process_joint(nextJ(ld), fb(ld).basis(), fb(ld).width(), hi, xrtj(MIDDLE_TIP));
			break;
		case Leap::Finger::Type::TYPE_RING:
			// Ad nauseum.
			ulv2_process_joint(prevJ(lm), fb(lm).basis(), fb(lm).width(), hi, xrtj(RING_METACARPAL));
			ulv2_process_joint(prevJ(lp), fb(lp).basis(), fb(lm).width(), hi, xrtj(RING_PROXIMAL));
			ulv2_process_joint(prevJ(li), fb(li).basis(), fb(lp).width(), hi, xrtj(RING_INTERMEDIATE));
			ulv2_process_joint(prevJ(ld), fb(ld).basis(), fb(li).width(), hi, xrtj(RING_DISTAL));
			ulv2_process_joint(nextJ(ld), fb(ld).basis(), fb(ld).width(), hi, xrtj(RING_TIP));
			break;
		case Leap::Finger::Type::TYPE_PINKY:
			ulv2_process_joint(prevJ(lm), fb(lm).basis(), fb(lm).width(), hi, xrtj(LITTLE_METACARPAL));
			ulv2_process_joint(prevJ(lp), fb(lp).basis(), fb(lm).width(), hi, xrtj(LITTLE_PROXIMAL));
			ulv2_process_joint(prevJ(li), fb(li).basis(), fb(lp).width(), hi, xrtj(LITTLE_INTERMEDIATE));
			ulv2_process_joint(prevJ(ld), fb(ld).basis(), fb(li).width(), hi, xrtj(LITTLE_DISTAL));
			ulv2_process_joint(nextJ(ld), fb(ld).basis(), fb(ld).width(), hi, xrtj(LITTLE_TIP));
			break;
			// I hear that Sagittarius has a better api, in C even, so hopefully
			// there'll be less weird boilerplate whenever we get that
		}
	}
}

void *
leap_input_loop(void *ptr_to_xdev)
{
	float retry_sleep_time = 0.05;
	float timeout = 0.5;
	int num_tries = (timeout / retry_sleep_time);
	bool succeeded_connected = false;
	bool succeeded_service_connected = false;

	struct xrt_device *xdev = (struct xrt_device *)ptr_to_xdev;
	struct ulv2_device *ulv2d = ulv2_device(xdev);
	ULV2_DEBUG(ulv2d, "num tries %d; sleep time %f", num_tries, timeout);

	Leap::Controller LeapController;
	os_nanosleep(U_1_000_000_000 * 0.01);
	// sleep for an arbitrary amount of time so that Leap::Controller can initialize and connect to the service.
	float start = (float)os_monotonic_get_ns() / (float)U_1_000_000_000;
	// would be nice to do this only if we're not building release ^^. don't know how to do that though.

	for (int i = 0; i < num_tries; i++) {
		succeeded_connected = LeapController.isConnected();
		succeeded_service_connected = LeapController.isServiceConnected();
		if (succeeded_connected) {
			ULV2_INFO(ulv2d, "Leap Motion Controller connected!");
			break;
		}
		if (succeeded_service_connected) {
			ULV2_INFO(ulv2d,
			          "Connected to Leap service, but not "
			          "connected to Leap Motion "
			          "controller. Retrying (%i / %i)",
			          i, num_tries);
			// This codepath should very rarely be enter as nowadays this gets probed by VID/PID, so you'd
			// have to be pretty fast to unplug after it gets probed and before this check.
		} else {
			ULV2_INFO(ulv2d,
			          "Not connected to Leap service. "
			          "Retrying (%i / %i)",
			          i, num_tries);
		}
		os_nanosleep(U_1_000_000_000 * retry_sleep_time); // 1 second * retry_sleep_time
	}

	ULV2_DEBUG(ulv2d, "Waited %f seconds", ((float)os_monotonic_get_ns() / (float)U_1_000_000_000) - start);

	bool hmdpolicyset = false;


	if (!succeeded_connected) {
		if (succeeded_service_connected) {
			ULV2_INFO(ulv2d,
			          "Connected to Leap service, but couldn't "
			          "connect to leap motion controller.\n"
			          "Is it plugged in and has your Leap service "
			          "detected it?");
			// ditto on this codepath
		} else {
			ULV2_INFO(ulv2d,
			          "Couldn't connect to Leap service. Try "
			          "running sudo leapd in another terminal.");
		}
		goto cleanup_leap_loop;
	}

	// Try to let the Leap service know that we are on a HMD, not on a desk.
	for (int i = 0; i < num_tries; i++) {
		LeapController.setPolicy(Leap::Controller::POLICY_OPTIMIZE_HMD);
		os_nanosleep(time_s_to_ns(0.02));
		LeapController.setPolicy(Leap::Controller::POLICY_OPTIMIZE_HMD);
		hmdpolicyset = LeapController.isPolicySet(Leap::Controller::POLICY_OPTIMIZE_HMD);
		if (!hmdpolicyset) {
			ULV2_ERROR(ulv2d, "Couldn't set HMD policy. Retrying (%i / %i)", i, num_tries);
		} else {
			ULV2_DEBUG(ulv2d, "HMD policy set.");
			break;
		}
		os_nanosleep(U_1_000_000_000 * retry_sleep_time); // 1 second * retry_sleep_time
	}
	ULV2_TRACE(ulv2d, "thread OK\n");
	ulv2d->our_thread_status = THREAD_OK;


	// Main loop
	while (!ulv2d->pthread_should_stop) {

		if (!LeapController.isConnected()) {
			if ((error_time % 100) == 0)
				ULV2_ERROR(ulv2d, "LeapController is not connected\n");
			os_nanosleep(U_1_000_000_000 * 0.1);
			error_time += 1;
			continue;
		}
		error_time = 100; // if there's an error next time, the modulo will return 0.

		Leap::Frame frame = LeapController.frame();
		Leap::HandList hands = frame.hands();
		bool leftbeendone = false;
		bool rightbeendone = false;
		for (Leap::HandList::const_iterator hl = hands.begin(); hl != hands.end(); ++hl) {
			int hi; // hand index
			const Leap::Hand hand = *hl;
			// if (hand.confidence() < *hand_min_confidence)
			// 	continue;
			if (hand.isLeft()) {
				if (leftbeendone)
					continue; // in case there are more than one left hand
				leftbeendone = true;
				hi = 0;
			} else if (hand.isRight()) {
				if (rightbeendone)
					continue; // in case there are more than one right hand
				rightbeendone = true;
				hi = 1;
			} else {
				continue;
			}

			ulv2_process_hand(hand, &ulv2d->joints_write_in[hi], hi);
		}
		os_thread_helper_lock(&ulv2d->leap_loop_oth);
		memcpy(&ulv2d->joints_read_out, &ulv2d->joints_write_in, sizeof(struct xrt_hand_joint_set) * 2);
		//! @todo (Moses Turner) Could be using LeapController.now() to try to emulate our own pose prediction,
		//! but I ain't got time for that
		ulv2d->hand_exists[0] = leftbeendone;
		ulv2d->hand_exists[1] = rightbeendone;
		os_thread_helper_unlock(&ulv2d->leap_loop_oth);
	}

cleanup_leap_loop:
	ULV2_TRACE(ulv2d, "leaving input thread\n");
	ulv2d->our_thread_status = THREAD_ERRORED_OUT;
	pthread_exit(NULL);
}

static void
ulv2_device_update_inputs(struct xrt_device *xdev)
{
	// Empty
}


static void
ulv2_device_get_hand_tracking(struct xrt_device *xdev,
                              enum xrt_input_name name,
                              uint64_t at_timestamp_ns,
                              struct xrt_hand_joint_set *out_value,
                              uint64_t *out_timestamp_ns)
{
	struct ulv2_device *ulv2d = ulv2_device(xdev);

	if (name != XRT_INPUT_GENERIC_HAND_TRACKING_LEFT && name != XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
		ULV2_ERROR(ulv2d, "unknown input name for hand tracker");
		return;
	}

	bool hand_index = (name == XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT); // 0 if left, 1 if right.

	bool hand_valid = ulv2d->hand_exists[hand_index];

	os_thread_helper_lock(&ulv2d->leap_loop_oth);
	memcpy(out_value, &ulv2d->joints_read_out[hand_index], sizeof(struct xrt_hand_joint_set));
	hand_valid = ulv2d->hand_exists[hand_index];
	os_thread_helper_unlock(&ulv2d->leap_loop_oth);
	m_space_relation_ident(&out_value->hand_pose);

	if (hand_valid) {
		out_value->is_active = true;
		out_value->hand_pose.relation_flags = valid_flags;
	} else {
		out_value->is_active = false;
	}
	// This is a lie - this driver does no pose-prediction or history. Patches welcome.
	*out_timestamp_ns = at_timestamp_ns;
}

static void
ulv2_device_destroy(struct xrt_device *xdev)
{
	struct ulv2_device *ulv2d = ulv2_device(xdev);

	ulv2d->pthread_should_stop = true;

	// Destroy also stops the thread.
	os_thread_helper_destroy(&ulv2d->leap_loop_oth);

	// Remove the variable tracking.
	u_var_remove_root(ulv2d);

	u_device_free(&ulv2d->base);
}

xrt_result_t
ulv2_create_device(struct xrt_device **out_xdev)
{
	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_NO_FLAGS;

	int num_hands = 2;

	struct ulv2_device *ulv2d = U_DEVICE_ALLOCATE(struct ulv2_device, flags, num_hands, 0);

	os_thread_helper_init(&ulv2d->leap_loop_oth);
	os_thread_helper_start(&ulv2d->leap_loop_oth, (&leap_input_loop), (void *)&ulv2d->base);

	ulv2d->base.tracking_origin = &ulv2d->tracking_origin;
	ulv2d->base.tracking_origin->type = XRT_TRACKING_TYPE_OTHER;

	math_pose_identity(&ulv2d->base.tracking_origin->offset);

	ulv2d->log_level = debug_get_log_option_ulv2_log();

	ulv2d->base.update_inputs = ulv2_device_update_inputs;
	ulv2d->base.get_hand_tracking = ulv2_device_get_hand_tracking;
	ulv2d->base.destroy = ulv2_device_destroy;

	strncpy(ulv2d->base.str, "Leap Motion v2 driver", XRT_DEVICE_NAME_LEN);
	strncpy(ulv2d->base.serial, "Leap Motion v2 driver", XRT_DEVICE_NAME_LEN);

	ulv2d->base.inputs[0].name = XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
	ulv2d->base.inputs[1].name = XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;

	ulv2d->base.name = XRT_DEVICE_HAND_TRACKER;

	ulv2d->base.device_type = XRT_DEVICE_TYPE_HAND_TRACKER;
	ulv2d->base.hand_tracking_supported = true;

	u_var_add_root(ulv2d, "Leap Motion v2 driver", true);
	u_var_add_ro_text(ulv2d, ulv2d->base.str, "Name");



	uint64_t start_time = os_monotonic_get_ns();
	uint64_t too_long = time_s_to_ns(15.0f);

	while (ulv2d->our_thread_status != THREAD_OK) {
		ULV2_TRACE(ulv2d, "waiting... thread status is %d", ulv2d->our_thread_status);
		if (ulv2d->our_thread_status == THREAD_ERRORED_OUT)
			goto cleanup;
		if ((os_monotonic_get_ns() - (uint64_t)start_time) > too_long) {
			ULV2_ERROR(ulv2d,
			           "For some reason the Leap thread locked up. This is a serious error and should "
			           "never happen.");
			goto cleanup;
		}
		os_nanosleep(time_s_to_ns(0.01));
	}


	ULV2_INFO(ulv2d, "Hand Tracker initialized!");

	out_xdev[0] = &ulv2d->base;

	return XRT_SUCCESS;

cleanup:
	ulv2_device_destroy(&ulv2d->base);
	return XRT_ERROR_DEVICE_CREATION_FAILED;
}

} // extern "C"
