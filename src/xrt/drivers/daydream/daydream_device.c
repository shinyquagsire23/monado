// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Daydream controller code.
 * @author Pete Black <pete.black@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_daydream
 */

#include "xrt/xrt_prober.h"
#include "xrt/xrt_tracking.h"

#include "os/os_time.h"

#include "math/m_api.h"
#include "tracking/t_imu.h"

#include "util/u_var.h"
#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_bitwise.h"

#include "daydream_device.h"

#include <stdio.h>
#include <math.h>
#include <assert.h>


/*!
 * Indices where each input is in the input list.
 */
enum daydream_input_index
{
	DAYDREAM_TOUCHPAD_CLICK,
	DAYDREAM_BAR_CLICK,
	DAYDREAM_CIRCLE_CLICK,
	DAYDREAM_VOLUP_CLICK,
	DAYDREAM_VOLDN_CLICK,
	DAYDREAM_TOUCHPAD_POSX,
	DAYDREAM_TOUCHPAD_POSY,

};

/*!
 * Input package for Daydream.
 */
struct daydream_input_packet
{
	uint8_t data[20];
};


/*
 *
 * Smaller helper functions.
 *
 */

static inline struct daydream_device *
daydream_device(struct xrt_device *xdev)
{
	return (struct daydream_device *)xdev;
}

static void
daydream_update_input_click(struct daydream_device *daydream,
                            int index,
                            int64_t now,
                            uint32_t bit)
{

	daydream->base.inputs[index].timestamp = now;
	daydream->base.inputs[index].value.boolean =
	    (daydream->last.buttons & bit) != 0;
}


/*
 *
 * Internal functions.
 *
 */

static void
update_fusion(struct daydream_device *daydream,
              struct daydream_parsed_sample *sample,
              timepoint_ns timestamp_ns,
              time_duration_ns delta_ns)
{
	DAYDREAM_DEBUG(daydream,
	               "fusion sample mx %d my %d mz %d ax %d ay %d az %d gx "
	               "%d gy %d gz %d\n",
	               sample->mag.x, sample->mag.y, sample->mag.z,
	               sample->accel.x, sample->accel.y, sample->accel.z,
	               sample->gyro.x, sample->gyro.y, sample->gyro.z);


	daydream->read.accel.x =
	    (sample->accel.x - daydream->calibration.accel.bias.x) /
	    daydream->calibration.accel.factor.x * MATH_GRAVITY_M_S2;
	daydream->read.accel.y =
	    (sample->accel.y - daydream->calibration.accel.bias.y) /
	    daydream->calibration.accel.factor.y * MATH_GRAVITY_M_S2;
	daydream->read.accel.z =
	    (sample->accel.z - daydream->calibration.accel.bias.z) /
	    daydream->calibration.accel.factor.z * MATH_GRAVITY_M_S2;

	daydream->read.gyro.x =
	    (sample->gyro.x - daydream->calibration.gyro.bias.x) /
	    daydream->calibration.gyro.factor.x;
	daydream->read.gyro.y =
	    (sample->gyro.y - daydream->calibration.gyro.bias.y) /
	    daydream->calibration.gyro.factor.y;
	daydream->read.gyro.z =
	    (sample->gyro.z - daydream->calibration.gyro.bias.z) /
	    daydream->calibration.gyro.factor.z;
	DAYDREAM_DEBUG(
	    daydream,
	    "fusion calibrated sample ax %f ay %f az %f gx %f gy %f gz %f\n",
	    daydream->read.accel.x, daydream->read.accel.y,
	    daydream->read.accel.z, daydream->read.gyro.x,
	    daydream->read.gyro.y, daydream->read.gyro.z);


	double delta_s = (double)delta_ns / (1000.0 * 1000.0 * 1000.0);
	math_quat_integrate_velocity(&daydream->fusion.rot,
	                             &daydream->read.gyro, delta_s,
	                             &daydream->fusion.rot);
}

static int
daydream_parse_input(struct daydream_device *daydream,
                     void *data,
                     struct daydream_parsed_input *input)
{
	U_ZERO(input);
	unsigned char *b = (unsigned char *)data;
	DAYDREAM_DEBUG(
	    daydream,
	    "raw input: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
	    "%02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
	    b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10],
	    b[11], b[12], b[13], b[14], b[15], b[16], b[17], b[18], b[19]);
	input->timestamp = get_bits(b, 0, 14);
	input->sample.mag.x = sign_extend_13(get_bits(b, 14, 13));
	input->sample.mag.y = sign_extend_13(get_bits(b, 27, 13));
	input->sample.mag.z = sign_extend_13(get_bits(b, 40, 13));
	input->sample.accel.x = sign_extend_13(get_bits(b, 53, 13));
	input->sample.accel.y = sign_extend_13(get_bits(b, 66, 13));
	input->sample.accel.z = sign_extend_13(get_bits(b, 79, 13));
	input->sample.gyro.x = sign_extend_13(get_bits(b, 92, 13));
	input->sample.gyro.y = sign_extend_13(get_bits(b, 105, 13));
	input->sample.gyro.z = sign_extend_13(get_bits(b, 118, 13));
	input->touchpad.x = get_bits(b, 131, 8);
	input->touchpad.y = get_bits(b, 139, 8);
	input->buttons |= get_bit(b, 147) << DAYDREAM_VOLUP_BUTTON_BIT;
	input->buttons |= get_bit(b, 148) << DAYDREAM_VOLDN_BUTTON_BIT;
	input->buttons |= get_bit(b, 149) << DAYDREAM_CIRCLE_BUTTON_BIT;
	input->buttons |= get_bit(b, 150) << DAYDREAM_BAR_BUTTON_BIT;
	input->buttons |= get_bit(b, 151) << DAYDREAM_TOUCHPAD_BUTTON_BIT;
	// DAYDREAM_DEBUG(daydream,"touchpad: %d %dx\n",
	// input->touchpad.x,input->touchpad.y);

	daydream->last = *input;
	return 1;
}

/*!
 * Reads one packet from the device,handles locking and checking if
 * the thread has been told to shut down.
 */
static bool
daydream_read_one_packet(struct daydream_device *daydream,
                         uint8_t *buffer,
                         size_t size)
{
	os_thread_helper_lock(&daydream->oth);

	while (os_thread_helper_is_running_locked(&daydream->oth)) {
		int retries = 5;
		int ret = -1;
		os_thread_helper_unlock(&daydream->oth);

		while (retries > 0) {
			ret = os_ble_read(daydream->ble, buffer, size, 500);
			if (ret == (int)size) {
				break;
			}
			retries--;
		}
		if (ret == 0) {
			fprintf(stderr, "%s\n", __func__);
			// Must lock thread before check in while.
			os_thread_helper_lock(&daydream->oth);
			continue;
		}
		if (ret < 0) {
			DAYDREAM_ERROR(daydream, "Failed to read device '%i'!",
			               ret);
			return false;
		}
		return true;
	}

	return false;
}

static void *
daydream_run_thread(void *ptr)
{
	struct daydream_device *daydream = (struct daydream_device *)ptr;
	//! @todo this should be injected at construction time
	struct time_state *time = time_state_create();

	uint8_t buffer[20];
	struct daydream_parsed_input input; // = {0};

	// wait for a package to sync up, it's discarded but that's okay.
	if (!daydream_read_one_packet(daydream, buffer, 20)) {
		// Does null checking and sets to null.
		time_state_destroy(&time);
		return NULL;
	}

	timepoint_ns then_ns = time_state_get_now(time);
	while (daydream_read_one_packet(daydream, buffer, 20)) {

		timepoint_ns now_ns = time_state_get_now(time);

		int num = daydream_parse_input(daydream, buffer, &input);
		(void)num;

		time_duration_ns delta_ns = now_ns - then_ns;
		then_ns = now_ns;

		// Lock last and the fusion.
		os_mutex_lock(&daydream->lock);


		// Process the parsed data.
		update_fusion(daydream, &input.sample, now_ns, delta_ns);

		// Now done.
		os_mutex_unlock(&daydream->lock);
	}

	// Does null checking and sets to null.
	time_state_destroy(&time);

	return NULL;
}

static int
daydream_get_calibration(struct daydream_device *daydream)
{
	return 0;
}


/*
 *
 * Device functions.
 *
 */

static void
daydream_get_fusion_pose(struct daydream_device *daydream,
                         enum xrt_input_name name,
                         timepoint_ns when,
                         struct xrt_space_relation *out_relation)
{
	out_relation->pose.orientation = daydream->fusion.rot;

	//! @todo assuming that orientation is actually currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
}

static void
daydream_device_destroy(struct xrt_device *xdev)
{
	struct daydream_device *daydream = daydream_device(xdev);

	// Destroy the thread object.
	os_thread_helper_destroy(&daydream->oth);

	// Now that the thread is not running we can destroy the lock.
	os_mutex_destroy(&daydream->lock);

	// Destroy the IMU fusion.
	imu_fusion_destroy(daydream->fusion.fusion);

	// Remove the variable tracking.
	u_var_remove_root(daydream);

	// Does null checking and zeros.
	os_ble_destroy(&daydream->ble);

	free(daydream);
}

static void
daydream_device_update_inputs(struct xrt_device *xdev,
                              struct time_state *timekeeping)
{
	struct daydream_device *daydream = daydream_device(xdev);

	int64_t now = time_state_get_now(timekeeping);

	// Lock the data.
	os_mutex_lock(&daydream->lock);

	// clang-format off
	daydream_update_input_click(daydream, 1, now, DAYDREAM_TOUCHPAD_BUTTON_MASK);
	daydream_update_input_click(daydream, 2, now, DAYDREAM_BAR_BUTTON_MASK);
	daydream_update_input_click(daydream, 3, now, DAYDREAM_CIRCLE_BUTTON_MASK);
	daydream_update_input_click(daydream, 4, now, DAYDREAM_VOLDN_BUTTON_MASK);
	daydream_update_input_click(daydream, 5, now, DAYDREAM_VOLUP_BUTTON_MASK);
	// clang-format on

	daydream->base.inputs[6].timestamp = now;
	daydream->base.inputs[6].value.vec1.x = daydream->last.touchpad.x;
	daydream->base.inputs[7].timestamp = now;
	daydream->base.inputs[7].value.vec1.x = daydream->last.touchpad.y;

	// Done now.

	os_mutex_unlock(&daydream->lock);
}

static void
daydream_device_get_tracked_pose(struct xrt_device *xdev,
                                 enum xrt_input_name name,
                                 struct time_state *timekeeping,
                                 int64_t *out_timestamp,
                                 struct xrt_space_relation *out_relation)
{
	struct daydream_device *daydream = daydream_device(xdev);

	timepoint_ns now = time_state_get_now(timekeeping);

	daydream_get_fusion_pose(daydream, name, now, out_relation);
}


/*
 *
 * Prober functions.
 *
 */

struct daydream_device *
daydream_device_create(struct os_ble_device *ble,
                       bool print_spew,
                       bool print_debug)
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_TRACKING_NONE);
	struct daydream_device *dd =
	    U_DEVICE_ALLOCATE(struct daydream_device, flags, 8, 0);

	dd->print_spew = print_spew;
	dd->print_debug = print_debug;
	dd->base.name = XRT_DEVICE_DAYDREAM;
	dd->base.destroy = daydream_device_destroy;
	dd->base.update_inputs = daydream_device_update_inputs;
	dd->base.get_tracked_pose = daydream_device_get_tracked_pose;
	dd->base.inputs[0].name = XRT_INPUT_DAYDREAM_POSE;
	dd->base.inputs[1].name = XRT_INPUT_DAYDREAM_TOUCHPAD_CLICK;
	dd->base.inputs[2].name = XRT_INPUT_DAYDREAM_BAR_CLICK;
	dd->base.inputs[3].name = XRT_INPUT_DAYDREAM_CIRCLE_CLICK;
	dd->base.inputs[4].name = XRT_INPUT_DAYDREAM_VOLDN_CLICK;
	dd->base.inputs[5].name = XRT_INPUT_DAYDREAM_VOLUP_CLICK;
	dd->base.inputs[6].name = XRT_INPUT_DAYDREAM_TOUCHPAD_VALUE_X;
	dd->base.inputs[7].name = XRT_INPUT_DAYDREAM_TOUCHPAD_VALUE_Y;

	dd->ble = ble;
	dd->fusion.rot.w = 1.0f;
	dd->fusion.fusion = imu_fusion_create();
	dd->fusion.variance.accel.x = 1.0f;
	dd->fusion.variance.accel.y = 1.0f;
	dd->fusion.variance.accel.z = 1.0f;
	dd->fusion.variance.gyro.x = 1.0f;
	dd->fusion.variance.gyro.y = 1.0f;
	dd->fusion.variance.gyro.z = 1.0f;

	dd->calibration.accel.factor.x = 120.0;
	dd->calibration.accel.factor.y = 120.0;
	dd->calibration.accel.factor.z = 120.0;

	dd->calibration.accel.bias.x = 0.0;
	dd->calibration.accel.bias.y = 0.0;
	dd->calibration.accel.bias.z = 0.0;

	dd->calibration.gyro.factor.x = 120.0;
	dd->calibration.gyro.factor.y = 120.0;
	dd->calibration.gyro.factor.z = 120.0;

	dd->calibration.gyro.bias.x = 0.0;
	dd->calibration.gyro.bias.y = 0.0;
	dd->calibration.gyro.bias.z = 0.0;

	daydream_get_calibration(dd);

	// Everything done, finally start the thread.
	int ret = os_thread_helper_start(&dd->oth, daydream_run_thread, dd);
	if (ret != 0) {
		DAYDREAM_ERROR(dd, "Failed to start thread!");
		daydream_device_destroy(&dd->base);
		return NULL;
	}


	DAYDREAM_DEBUG(dd, "Created device!");

	return dd;
}
