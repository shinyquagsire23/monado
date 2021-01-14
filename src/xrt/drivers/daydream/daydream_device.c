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

DEBUG_GET_ONCE_LOG_OPTION(daydream_log, "DAYDREAM_LOG", U_LOGGING_WARN)

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
	DAYDREAM_TOUCHPAD,

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
daydream_update_input_click(struct daydream_device *daydream, int index, int64_t now, uint32_t bit)
{

	daydream->base.inputs[index].timestamp = now;
	daydream->base.inputs[index].value.boolean = (daydream->last.buttons & bit) != 0;
}


/*
 *
 * Internal functions.
 *
 */

static void
update_fusion(struct daydream_device *dd,
              struct daydream_parsed_sample *sample,
              timepoint_ns timestamp_ns,
              time_duration_ns delta_ns)
{

	struct xrt_vec3 accel, gyro;
	m_imu_pre_filter_data(&dd->pre_filter, &sample->accel, &sample->gyro, &accel, &gyro);

	DAYDREAM_DEBUG(dd,
	               "fusion sample"
	               " (mx %d my %d mz %d)"
	               " (ax %d ay %d az %d)"
	               " (gx %d gy %d gz %d)",
	               sample->mag.x, sample->mag.y, sample->mag.z, sample->accel.x, sample->accel.y, sample->accel.z,
	               sample->gyro.x, sample->gyro.y, sample->gyro.z);
	DAYDREAM_DEBUG(dd,
	               "fusion calibrated sample"
	               " (ax %f ay %f az %f)"
	               " (gx %f gy %f gz %f)",
	               accel.x, accel.y, accel.z, gyro.x, gyro.y, gyro.z);
	DAYDREAM_DEBUG(dd, "-");

	m_imu_3dof_update(&dd->fusion, timestamp_ns, &accel, &gyro);
}

static int
daydream_parse_input(struct daydream_device *daydream, void *data, struct daydream_parsed_input *input)
{
	U_ZERO(input);
	unsigned char *b = (unsigned char *)data;
	DAYDREAM_DEBUG(daydream,
	               "raw input: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
	               "%02x %02x %02x %02x %02x %02x %02x %02x %02x",
	               b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14],
	               b[15], b[16], b[17], b[18], b[19]);
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
daydream_read_one_packet(struct daydream_device *daydream, uint8_t *buffer, size_t size)
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
			U_LOG_W("Retrying Bluetooth read.");
			// Must lock thread before check in while.
			os_thread_helper_lock(&daydream->oth);
			continue;
		}
		if (ret < 0) {
			DAYDREAM_ERROR(daydream, "Failed to read device '%i'!", ret);
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
                         struct xrt_space_relation *out_relation)
{
	out_relation->pose.orientation = daydream->fusion.rot;

	//! @todo assuming that orientation is actually currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
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

	// Destroy the fusion.
	m_imu_3dof_close(&daydream->fusion);

	// Remove the variable tracking.
	u_var_remove_root(daydream);

	// Does null checking and zeros.
	os_ble_destroy(&daydream->ble);

	free(daydream);
}

static void
daydream_device_update_inputs(struct xrt_device *xdev)
{
	struct daydream_device *daydream = daydream_device(xdev);

	uint64_t now = os_monotonic_get_ns();

	// Lock the data.
	os_mutex_lock(&daydream->lock);

	// clang-format off
	daydream_update_input_click(daydream, 1, now, DAYDREAM_TOUCHPAD_BUTTON_MASK);
	daydream_update_input_click(daydream, 2, now, DAYDREAM_BAR_BUTTON_MASK);
	daydream_update_input_click(daydream, 3, now, DAYDREAM_CIRCLE_BUTTON_MASK);
	daydream_update_input_click(daydream, 4, now, DAYDREAM_VOLDN_BUTTON_MASK);
	daydream_update_input_click(daydream, 5, now, DAYDREAM_VOLUP_BUTTON_MASK);
	// clang-format on

	daydream->base.inputs[DAYDREAM_TOUCHPAD].timestamp = now;
	float x = (daydream->last.touchpad.y / 255.0) * 2.0 - 1.0;
	float y = (daydream->last.touchpad.y / 255.0) * 2.0 - 1.0;

	// Device sets x and y to zero when no finger is detected on the pad.
	if (daydream->last.touchpad.x == 0 || daydream->last.touchpad.y == 0) {
		x = y = 0.0f;
	}

	daydream->base.inputs[DAYDREAM_TOUCHPAD].value.vec2.x = x;
	daydream->base.inputs[DAYDREAM_TOUCHPAD].value.vec2.y = y;

	// Done now.

	os_mutex_unlock(&daydream->lock);
}

static void
daydream_device_get_tracked_pose(struct xrt_device *xdev,
                                 enum xrt_input_name name,
                                 uint64_t at_timestamp_ns,
                                 struct xrt_space_relation *out_relation)
{
	struct daydream_device *daydream = daydream_device(xdev);

	(void)at_timestamp_ns;
	daydream_get_fusion_pose(daydream, name, out_relation);
}


/*
 *
 * Bindings
 *
 */

static struct xrt_binding_input_pair simple_inputs[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_DAYDREAM_BAR_CLICK},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_DAYDREAM_CIRCLE_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_DAYDREAM_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_DAYDREAM_POSE},
};

static struct xrt_binding_profile binding_profiles[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs,
        .num_inputs = ARRAY_SIZE(simple_inputs),
        .outputs = NULL,
        .num_outputs = 0,
    },
};


/*
 *
 * Prober functions.
 *
 */

struct daydream_device *
daydream_device_create(struct os_ble_device *ble)
{
	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)(U_DEVICE_ALLOC_TRACKING_NONE);
	struct daydream_device *dd = U_DEVICE_ALLOCATE(struct daydream_device, flags, 8, 0);

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
	dd->base.inputs[6].name = XRT_INPUT_DAYDREAM_TOUCHPAD;
	dd->base.binding_profiles = binding_profiles;
	dd->base.num_binding_profiles = ARRAY_SIZE(binding_profiles);

	dd->ble = ble;
	dd->ll = debug_get_log_option_daydream_log();

	float accel_ticks_to_float = MATH_GRAVITY_M_S2 / 520.0;
	float gyro_ticks_to_float = 1.0 / 120.0;
	m_imu_pre_filter_init(&dd->pre_filter, accel_ticks_to_float, gyro_ticks_to_float);
	m_imu_3dof_init(&dd->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_300MS);

	daydream_get_calibration(dd);

	// Everything done, finally start the thread.
	int ret = os_thread_helper_start(&dd->oth, daydream_run_thread, dd);
	if (ret != 0) {
		DAYDREAM_ERROR(dd, "Failed to start thread!");
		daydream_device_destroy(&dd->base);
		return NULL;
	}

	u_var_add_root(dd, "Daydream controller", true);
	u_var_add_gui_header(dd, &dd->gui.last, "Last");
	u_var_add_ro_vec3_f32(dd, &dd->fusion.last.accel, "last.accel");
	u_var_add_ro_vec3_f32(dd, &dd->fusion.last.gyro, "last.gyro");

	dd->base.orientation_tracking_supported = true;
	dd->base.position_tracking_supported = false;
	dd->base.device_type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER;

	DAYDREAM_DEBUG(dd, "Created device!");

	return dd;
}
