// Copyright 2020, Collabora, Ltd.
// Copyright 2016 Philipp Zabel
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vive Controller prober and driver code
 * @author Christoph Haag <christoph.gaag@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * Portions based on the VRPN Razer Hydra driver,
 * originally written by Ryan Pavlik and available under the BSL-1.0.
 */


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xrt/xrt_prober.h"

#include "math/m_api.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_json.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "os/os_hid.h"
#include "os/os_threading.h"
#include "os/os_time.h"

#include "../vive/vive_protocol.h"
#include "vive_controller_interface.h"

#include "math/m_imu_3dof.h"

#ifdef XRT_OS_LINUX
#include <unistd.h>
#include <math.h>
#endif

/*
 *
 * Defines & structs.
 *
 */

#define VIVE_CONTROLLER_SPEW(p, ...)                                           \
	do {                                                                   \
		if (p->print_spew) {                                           \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define VIVE_CONTROLLER_DEBUG(p, ...)                                          \
	do {                                                                   \
		if (p->print_debug) {                                          \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define VIVE_CONTROLLER_ERROR(p, ...)                                          \
	do {                                                                   \
		fprintf(stderr, "%s - ", __func__);                            \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, "\n");                                         \
	} while (false)

DEBUG_GET_ONCE_BOOL_OPTION(vive_controller_spew,
                           "VIVE_CONTROLLER_PRINT_SPEW",
                           false)
DEBUG_GET_ONCE_BOOL_OPTION(vive_controller_debug,
                           "VIVE_CONTROLLER_PRINT_DEBUG",
                           false)
enum vive_controller_input_index
{
	// common inputs
	VIVE_CONTROLLER_INDEX_AIM_POSE = 0,
	VIVE_CONTROLLER_INDEX_GRIP_POSE,
	VIVE_CONTROLLER_INDEX_SYSTEM_CLICK,
	VIVE_CONTROLLER_INDEX_TRIGGER_CLICK,
	VIVE_CONTROLLER_INDEX_TRIGGER_VALUE,
	VIVE_CONTROLLER_INDEX_TRACKPAD_X,
	VIVE_CONTROLLER_INDEX_TRACKPAD_Y,
	VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH,

	// Vive Wand specific inputs
	VIVE_CONTROLLER_INDEX_SQUEEZE_CLICK,
	VIVE_CONTROLLER_INDEX_MENU_CLICK,
	VIVE_CONTROLLER_INDEX_TRACKPAD_CLICK,

	// Valve Index specific inputs
	VIVE_CONTROLLER_INDEX_THUMBSTICK_X,
	VIVE_CONTROLLER_INDEX_THUMBSTICK_Y,
	VIVE_CONTROLLER_INDEX_A_CLICK,
	VIVE_CONTROLLER_INDEX_B_CLICK,
	VIVE_CONTROLLER_INDEX_THUMBSTICK_CLICK,

	VIVE_CONTROLLER_MAX_INDEX,
};

#define VIVE_CLOCK_FREQ 48000000.0f // Hz = 48 MHz

enum watchman_gen
{
	WATCHMAN_GEN1,
	WATCHMAN_GEN2,
	WATCHMAN_GEN_UNKNOWN
};

enum controller_variant
{
	CONTROLLER_VIVE_WAND,
	CONTROLLER_INDEX_LEFT,
	CONTROLLER_INDEX_RIGHT,
	CONTROLLER_UNKNOWN
};

#define DEFAULT_HAPTIC_FREQ 150.0f
#define MIN_HAPTIC_DURATION 0.05f

/*!
 * A Vive Controller device, representing just a single controller.
 *
 * @ingroup drv_vive
 */
struct vive_controller_device
{
	struct xrt_device base;

	struct os_hid_device *controller_hid;
	struct os_thread_helper controller_thread;

	struct
	{
		uint64_t time_ns;
		uint32_t last_sample_time_raw;
		double acc_range;
		double gyro_range;
		struct xrt_vec3 acc_bias;
		struct xrt_vec3 acc_scale;
		struct xrt_vec3 gyro_bias;
		struct xrt_vec3 gyro_scale;

		//! IMU position in tracking space.
		struct xrt_pose trackref;
	} imu;

	struct m_imu_3dof fusion;

	struct
	{
		struct xrt_vec3 acc;
		struct xrt_vec3 gyro;
	} last;

	struct xrt_quat rot_filtered;

	bool print_spew;
	bool print_debug;

	uint32_t last_ticks;

	//! Which vive controller in the system are we?
	size_t index;

	struct
	{
		struct xrt_vec2 trackpad;
		float trigger;
		uint8_t buttons;
		uint8_t last_buttons;

		bool charging;
		uint8_t battery;
	} state;

	struct
	{
		uint32_t firmware_version;
		uint8_t hardware_revision;
		uint8_t hardware_version_micro;
		uint8_t hardware_version_minor;
		uint8_t hardware_version_major;
		char *mb_serial_number;
		char *model_number;
		char *device_serial_number;
	} firmware;

	enum watchman_gen watchman_gen;
	enum controller_variant variant;
};

static inline struct vive_controller_device *
vive_controller_device(struct xrt_device *xdev)
{
	assert(xdev);
	struct vive_controller_device *ret =
	    (struct vive_controller_device *)xdev;
	return ret;
}

static void
vive_controller_device_destroy(struct xrt_device *xdev)
{
	struct vive_controller_device *d = vive_controller_device(xdev);

	os_thread_helper_destroy(&d->controller_thread);

	m_imu_3dof_close(&d->fusion);

	if (d->controller_hid)
		os_hid_destroy(d->controller_hid);

	free(d);
}

static void
vive_controller_device_update_wand_inputs(struct xrt_device *xdev)
{
	struct vive_controller_device *d = vive_controller_device(xdev);

	os_thread_helper_lock(&d->controller_thread);
	uint8_t buttons = d->state.buttons;

	/*
	int i = 8;
	while(i--) {
	        putchar('0' + ((buttons >> i) & 1));
	}
	printf("\n");
	*/


	uint64_t now = os_monotonic_get_ns();

	/* d->state.buttons is bitmask of currently pressed buttons.
	 * (index n) nth bit in the bitmask -> input "name"
	 */
	const int button_index_map[] = {VIVE_CONTROLLER_INDEX_TRIGGER_CLICK,
	                                VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH,
	                                VIVE_CONTROLLER_INDEX_TRACKPAD_CLICK,
	                                VIVE_CONTROLLER_INDEX_SYSTEM_CLICK,
	                                VIVE_CONTROLLER_INDEX_SQUEEZE_CLICK,
	                                VIVE_CONTROLLER_INDEX_MENU_CLICK};

	int button_count = ARRAY_SIZE(button_index_map);
	for (int i = 0; i < button_count; i++) {

		bool pressed = (buttons >> i) & 1;
		bool last_pressed = (d->state.last_buttons >> i) & 1;

		if (pressed != last_pressed) {
			struct xrt_input *input =
			    &d->base.inputs[button_index_map[i]];

			input->timestamp = now;
			input->value.boolean = pressed;

			VIVE_CONTROLLER_DEBUG(d, "button %d %s\n", i,
			                      pressed ? "pressed" : "released");
		}
	}

	if (d->state.trackpad.x != 0) {
		struct xrt_input *input =
		    &d->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_X];
		input->timestamp = now;
		input->value.vec1.x = d->state.trackpad.x;
	}

	if (d->state.trackpad.y != 0) {
		struct xrt_input *input =
		    &d->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_Y];
		input->timestamp = now;
		input->value.vec1.x = d->state.trackpad.y;
	}

	if (d->state.trackpad.x != 0 || d->state.trackpad.y != 0)
		VIVE_CONTROLLER_DEBUG(d, "Trackpad: %f, %f",
		                      d->state.trackpad.x, d->state.trackpad.y);

	if (d->state.trigger != 0) {
		struct xrt_input *input =
		    &d->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_VALUE];
		input->timestamp = now;
		input->value.vec1.x = d->state.trigger;
		VIVE_CONTROLLER_DEBUG(d, "Trigger: %f", d->state.trigger);
	}

	d->state.last_buttons = d->state.buttons;
	os_thread_helper_unlock(&d->controller_thread);
}

static void
vive_controller_device_update_index_inputs(struct xrt_device *xdev)
{
	struct vive_controller_device *d = vive_controller_device(xdev);

	os_thread_helper_lock(&d->controller_thread);
	uint8_t buttons = d->state.buttons;

	/*
	int i = 8;
	while(i--) {
	        putchar('0' + ((buttons >> i) & 1));
	}
	printf("\n");
	*/

	uint64_t now = os_monotonic_get_ns();

	/* d->state.buttons is bitmask of currently pressed buttons.
	 * (index n) nth bit in the bitmask -> input "name"
	 */
	const int button_index_map[] = {VIVE_CONTROLLER_INDEX_TRIGGER_CLICK,
	                                VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH,
	                                VIVE_CONTROLLER_INDEX_THUMBSTICK_CLICK,
	                                VIVE_CONTROLLER_INDEX_SYSTEM_CLICK,
	                                VIVE_CONTROLLER_INDEX_A_CLICK,
	                                VIVE_CONTROLLER_INDEX_B_CLICK};

	int button_count = ARRAY_SIZE(button_index_map);
	for (int i = 0; i < button_count; i++) {

		bool pressed = (buttons >> i) & 1;
		bool last_pressed = (d->state.last_buttons >> i) & 1;

		if (pressed != last_pressed) {
			struct xrt_input *input =
			    &d->base.inputs[button_index_map[i]];

			input->timestamp = now;
			input->value.boolean = pressed;

			VIVE_CONTROLLER_DEBUG(d, "button %d %s\n", i,
			                      pressed ? "pressed" : "released");
		}
	}

	/* trackpad and thumbstick position are the same usb events.
	 * report trackpad position when trackpad has been touched last, and
	 * thumbstick position when trackpad touch has been released
	 */
	if (d->state.trackpad.x != 0) {
		struct xrt_input *input;
		if (d->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH]
		        .value.boolean)
			input =
			    &d->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_X];
		else
			input =
			    &d->base.inputs[VIVE_CONTROLLER_INDEX_THUMBSTICK_X];
		input->timestamp = now;
		input->value.vec1.x = d->state.trackpad.x;
	}

	if (d->state.trackpad.y != 0) {
		struct xrt_input *input;
		if (d->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH]
		        .value.boolean)
			input =
			    &d->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_X];
		else
			input =
			    &d->base.inputs[VIVE_CONTROLLER_INDEX_THUMBSTICK_X];
		input->timestamp = now;
		input->value.vec1.x = d->state.trackpad.y;
	}

	if (d->state.trackpad.x != 0 || d->state.trackpad.y != 0) {
		const char *component =
		    d->base.inputs[VIVE_CONTROLLER_INDEX_TRACKPAD_TOUCH]
		            .value.boolean
		        ? "Trackpad"
		        : "Thumbstick";
		VIVE_CONTROLLER_DEBUG(d, "%s: %f, %f", component,
		                      d->state.trackpad.x, d->state.trackpad.y);
	}

	if (d->state.trigger != 0) {
		struct xrt_input *input =
		    &d->base.inputs[VIVE_CONTROLLER_INDEX_TRIGGER_VALUE];
		input->timestamp = now;
		input->value.vec1.x = d->state.trigger;
		VIVE_CONTROLLER_DEBUG(d, "Trigger: %f", d->state.trigger);
	}

	d->state.last_buttons = d->state.buttons;
	os_thread_helper_unlock(&d->controller_thread);
}

static void
vive_controller_device_get_tracked_pose(struct xrt_device *xdev,
                                        enum xrt_input_name name,
                                        uint64_t at_timestamp_ns,
                                        uint64_t *out_relation_timestamp_ns,
                                        struct xrt_space_relation *out_relation)
{
	struct vive_controller_device *d = vive_controller_device(xdev);

	// printf("input name %d %d\n", name, XRT_INPUT_VIVE_GRIP_POSE);
	if (name != XRT_INPUT_VIVE_AIM_POSE &&
	    name != XRT_INPUT_VIVE_GRIP_POSE &&
	    name != XRT_INPUT_INDEX_AIM_POSE &&
	    name != XRT_INPUT_INDEX_GRIP_POSE) {
		VIVE_CONTROLLER_ERROR(d, "unknown input name");
		return;
	}

	// Clear out the relation.
	U_ZERO(out_relation);

	uint64_t now = os_monotonic_get_ns();
	*out_relation_timestamp_ns = now;

	os_thread_helper_lock(&d->controller_thread);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&d->controller_thread)) {
		os_thread_helper_unlock(&d->controller_thread);
		return;
	}

	out_relation->pose.orientation = d->rot_filtered;

	//! @todo assuming that orientation is actually currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_POSITION_TRACKED_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

	os_thread_helper_unlock(&d->controller_thread);

	struct xrt_vec3 pos = out_relation->pose.position;
	struct xrt_quat quat = out_relation->pose.orientation;
	VIVE_CONTROLLER_SPEW(
	    d, "GET_TRACKED_POSE (%f, %f, %f) (%f, %f, %f, %f) ", pos.x, pos.y,
	    pos.z, quat.x, quat.y, quat.z, quat.w);
}

static int
vive_controller_haptic_pulse(struct vive_controller_device *d,
                             union xrt_output_value *value)
{
	float duration_seconds;
	//! @todo: proper min duration value
	if (value->vibration.duration == -1) {
		VIVE_CONTROLLER_SPEW(d,
		                     "Haptic pulse duration: using %f minimum",
		                     MIN_HAPTIC_DURATION);
		duration_seconds = 0.1;
	} else {
		duration_seconds = time_ns_to_s(value->vibration.duration);
	}

	VIVE_CONTROLLER_SPEW(d, "Haptic pulse amp %f, %fHz, %fs",
	                     value->vibration.amplitude,
	                     value->vibration.frequency, duration_seconds);
	float frequency = value->vibration.frequency;

	//! @todo: proper unspecified value
	if (frequency == 0) {
		VIVE_CONTROLLER_SPEW(
		    d, "Haptic pulse frequency unspecified, setting to %fHz",
		    DEFAULT_HAPTIC_FREQ);
		frequency = 200;
	}


	/* haptic pulse for Vive Controller:
	 * desired_frequency = 1000 * 1000 / (high + low).
	 * => (high + low) = 1000 * 1000 / desired_frequency
	 * repeat = desired_duration_in_seconds * desired_frequency.
	 *
	 * I think:
	 * Lowest amplitude: 1, high+low-1
	 * Highest amplitude: (high+low)/2, / (high+low)/2
	 */

	float high_plus_low = 1000.f * 1000.f / frequency;
	uint16_t pulse_low =
	    (uint16_t)(value->vibration.amplitude * high_plus_low / 2.);

	/* Vive Controller doesn't vibrate with value == 0.
	 * Not sure if this actually happens, but let's fix it anyway. */
	if (pulse_low == 0)
		pulse_low = 1;

	uint16_t pulse_high = high_plus_low - pulse_low;

	uint16_t repeat_count = duration_seconds * frequency;

	const struct vive_controller_haptic_pulse_report report = {
	    .id = VIVE_CONTROLLER_COMMAND_REPORT_ID,
	    .command = VIVE_CONTROLLER_HAPTIC_PULSE_COMMAND,
	    .len = 7,
	    .zero = 0x00,
	    .pulse_high = __cpu_to_le16(pulse_high),
	    .pulse_low = __cpu_to_le16(pulse_low),
	    .repeat_count = __cpu_to_le16(repeat_count),
	};

	return os_hid_set_feature(d->controller_hid, (uint8_t *)&report,
	                          sizeof(report));
}

static void
vive_controller_device_set_output(struct xrt_device *xdev,
                                  enum xrt_output_name name,
                                  union xrt_output_value *value)
{
	struct vive_controller_device *d = vive_controller_device(xdev);

	if (name != XRT_OUTPUT_NAME_VIVE_HAPTIC &&
	    name != XRT_OUTPUT_NAME_INDEX_HAPTIC) {
		VIVE_CONTROLLER_ERROR(d, "Unknown output\n");
		return;
	}

	bool pulse = value->vibration.amplitude > 0.01;
	if (!pulse) {
		return;
	}

	vive_controller_haptic_pulse(d, value);
}

static void
controller_handle_battery(struct vive_controller_device *d, uint8_t battery)
{
	uint8_t charge_percent = battery & VIVE_CONTROLLER_BATTERY_CHARGE_MASK;
	bool charging = battery & VIVE_CONTROLLER_BATTERY_CHARGING;
	VIVE_CONTROLLER_DEBUG(d, "Charging %d, percent %d\n", charging,
	                      charge_percent);
	d->state.charging = charging;
	d->state.battery = charge_percent;
}

static void
controller_handle_buttons(struct vive_controller_device *d, uint8_t buttons)
{
	d->state.buttons = buttons;
}

static void
controller_handle_touch_position(struct vive_controller_device *d, uint8_t *buf)
{
	int16_t x = __le16_to_cpup((__le16 *)buf);
	int16_t y = __le16_to_cpup((__le16 *)(buf + 2));
	d->state.trackpad.x = (float)x / INT16_MAX;
	d->state.trackpad.y = (float)y / INT16_MAX;
	if (d->state.trackpad.x != 0 || d->state.trackpad.y != 0)
		VIVE_CONTROLLER_SPEW(d, "Trackpad %f,%f\n", d->state.trackpad.x,
		                     d->state.trackpad.y);
}

static void
controller_handle_analog_trigger(struct vive_controller_device *d,
                                 uint8_t analog)
{
	d->state.trigger = (float)analog / UINT8_MAX;
	VIVE_CONTROLLER_SPEW(d, "Trigger %f\n", d->state.trigger);
}

static inline uint32_t
calc_dt_raw_and_handle_overflow(struct vive_controller_device *d,
                                uint32_t sample_time)
{
	uint64_t dt_raw =
	    (uint64_t)sample_time - (uint64_t)d->imu.last_sample_time_raw;
	d->imu.last_sample_time_raw = sample_time;

	// The 32-bit tick counter has rolled over,
	// adjust the "negative" value to be positive.
	// It's easiest to do this with 64-bits.
	if (dt_raw > 0xFFFFFFFF) {
		dt_raw += 0x100000000;
	}

	return (uint32_t)dt_raw;
}

static inline uint64_t
cald_dt_ns(uint32_t dt_raw)
{
	double f = (double)(dt_raw) / VIVE_CLOCK_FREQ;
	uint64_t diff_ns = (uint64_t)(f * 1000.0 * 1000.0 * 1000.0);
	return diff_ns;
}

static void
vive_controller_handle_imu_sample(struct vive_controller_device *d,
                                  struct vive_imu_report *report)
{
	/* Time in 48 MHz ticks, but we are missing the low byte */
	uint32_t time_raw = d->last_ticks | report->id;
	uint32_t dt_raw = calc_dt_raw_and_handle_overflow(d, time_raw);
	uint64_t dt_ns = cald_dt_ns(dt_raw);

	int16_t acc[3] = {
	    __le16_to_cpu(report->sample->acc[0]),
	    __le16_to_cpu(report->sample->acc[1]),
	    __le16_to_cpu(report->sample->acc[2]),
	};

	int16_t gyro[3] = {
	    __le16_to_cpu(report->sample->gyro[0]),
	    __le16_to_cpu(report->sample->gyro[1]),
	    __le16_to_cpu(report->sample->gyro[2]),
	};

	float scale = (float)d->imu.acc_range / 32768.0f;
	struct xrt_vec3 acceleration = {
	    scale * d->imu.acc_scale.x * acc[0] - d->imu.acc_bias.x,
	    scale * d->imu.acc_scale.y * acc[1] - d->imu.acc_bias.y,
	    scale * d->imu.acc_scale.z * acc[2] - d->imu.acc_bias.z,
	};

	scale = (float)d->imu.gyro_range / 32768.0f;
	struct xrt_vec3 angular_velocity = {
	    scale * d->imu.gyro_scale.x * gyro[0] - d->imu.gyro_bias.x,
	    scale * d->imu.gyro_scale.y * gyro[1] - d->imu.gyro_bias.y,
	    scale * d->imu.gyro_scale.z * gyro[2] - d->imu.gyro_bias.z,
	};

	/*
	 VIVE_CONTROLLER_SPEW(d, "ACC  %f %f %f", acceleration.x,
	 acceleration.y, acceleration.z); VIVE_CONTROLLER_SPEW(d, "GYRO %f %f
	 %f", angular_velocity.x, angular_velocity.y, angular_velocity.z);
	 */

	if (d->variant == CONTROLLER_VIVE_WAND) {
		acceleration.x *= -1;
		float temp_accel = acceleration.y;
		acceleration.y = -acceleration.z;
		acceleration.z = -temp_accel;

		angular_velocity.x *= -1;
		float temp_ang = angular_velocity.y;
		angular_velocity.y = -angular_velocity.z;
		angular_velocity.z = -temp_ang;
	} else if (d->variant == CONTROLLER_INDEX_LEFT ||
	           d->variant == CONTROLLER_INDEX_RIGHT) {
		float temp_accel = acceleration.x;
		acceleration.x = acceleration.z;
		acceleration.y = -acceleration.y;
		acceleration.z = temp_accel;

		float temp_ang = angular_velocity.x;
		angular_velocity.x = angular_velocity.z;
		angular_velocity.y = -angular_velocity.y;
		angular_velocity.z = temp_ang;
	}

	d->imu.time_ns += dt_ns;
	d->last.acc = acceleration;
	d->last.gyro = angular_velocity;

	m_imu_3dof_update(&d->fusion, d->imu.time_ns, &acceleration,
	                  &angular_velocity);

	d->rot_filtered = d->fusion.rot;

	//      VIVE_CONTROLLER_SPEW(d, "Rot %f %f %f", d->rot_filtered.x,
	//                           d->rot_filtered.y, d->rot_filtered.z);
}

static void
vive_controller_handle_lighthousev1(uint8_t *buf, uint8_t len)
{
	// stub
}

/*
 * Handles battery, imu, trigger, buttons, trackpad.
 * Then hands off to vive_controller_handle_lighthousev1().
 */
static void
vive_controller_decode_watchmanv1(struct vive_controller_device *d,
                                  struct vive_controller_message *message)
{
	uint8_t *buf = message->payload;
	uint8_t *end = message->payload + message->len - 1;

	/*
	for (int i = 0; i < message->len; i++) {
	        //printf("%02x ", buf[i]);
	        int j = 8;
	        while(j--) {
	                putchar('0' + ((buf[i] >> j) & 1));
	        }
	        putchar(' ');
	}
	printf("\n");
	*/

	/* payload starts with "event flags" byte.
	 * If it does not start with 111, it contains only lighthouse data.
	 * If it starts with 111, events follow in this order, each of them
	 * optional:
	 *   - battery:  1 byte (1110???1)
	 *   - trigger:  1 byte (1111?1??)
	 *   - trackpad: 4 byte (1111??1?)
	 *   - buttons:  1 byte (1111???1)
	 *   - imu:     13 byte (111?1???)
	 * There may be another input event after a battery event.
	 * Lighthouse data may follow in the rest of the payload.
	 */

	// input events have first three bits set
	while ((*buf & 0xe0) == 0xe0 && buf < end) {

		// clang-format off

		// battery follows when 1110???1
		bool has_battery  = (*buf & 0x10) != 0x10 && (*buf & 0x1) == 0x1;

		// input follows when 1111?<trigger><trackpad><buttons>
		bool has_trigger  = (*buf & 0x10) == 0x10 && (*buf & 0x4) == 0x4;
		bool has_trackpad = (*buf & 0x10) == 0x10 && (*buf & 0x2) == 0x2;
		bool has_buttons  = (*buf & 0x10) == 0x10 && (*buf & 0x1) == 0x1;

		// imu event follows when 111?1???
		// there are imu-only messages, and imu-after-battery
		bool has_imu      = (*buf & 0x08) == 0x8;

		// clang-format on

		buf++;

		if (has_battery) {
			controller_handle_battery(d, *buf++);
		}

		if (has_buttons) {
			controller_handle_buttons(d, *buf++);
		}
		if (has_trigger) {
			controller_handle_analog_trigger(d, *buf++);
		}
		if (has_trackpad) {
			controller_handle_touch_position(d, buf);
			buf += 4;
		}
		if (has_imu) {
			vive_controller_handle_imu_sample(
			    d, (struct vive_imu_report *)buf);
			buf += 13;
		}
	}

	if (buf > end)
		VIVE_CONTROLLER_ERROR(d, "overshoot: %ld\n", buf - end);

	if (buf < end)
		vive_controller_handle_lighthousev1(buf, end - buf);
}

/*
 * Handles battery, imu, trigger, buttons, trackpad.
 * Then hands off to vive_controller_handle_lighthousev1().
 */
static void
vive_controller_decode_watchmanv2(struct vive_controller_device *d,
                                  struct vive_controller_message *message)
{
	uint8_t *buf = message->payload;
	uint8_t *end = message->payload + message->len - 1;

	/*
	for (int i = 0; i < message->len; i++) {
	        //printf("%02x ", buf[i]);
	        int j = 8;
	        while(j--) {
	                putchar('0' + ((buf[i] >> j) & 1));
	        }
	        putchar(' ');
	}
	printf("\n");
	*/

	/* payload starts with "event flags" byte.
	 * If it does not start with 111, it contains only lighthouse data,
	 * and possibly gen2 events.
	 * If it starts with 111, events follow in this order, each of them
	 * optional:
	 *   - battery:  1 byte (1110???1)
	 *   - trigger:  1 byte (1111?1??)
	 *   - trackpad: 4 byte (1111??1?)
	 *   - buttons:  1 byte (1111???1)
	 *   - imu:     13 byte (111?1???)
	 * There may be another input event after a battery event.
	 */

	// input events have first three bits set
	if ((*buf & 0xe0) == 0xe0 && buf < end) {

		// clang-format off

		// battery follows when 1110???1
		bool has_battery  = (*buf & 0x10) != 0x10 && (*buf & 0x1) == 0x1;

		// input follows when 1111?<trigger><trackpad><buttons>
		bool has_trigger  = (*buf & 0x10) == 0x10 && (*buf & 0x4) == 0x4;
		bool has_trackpad = (*buf & 0x10) == 0x10 && (*buf & 0x2) == 0x2;
		bool has_buttons  = (*buf & 0x10) == 0x10 && (*buf & 0x1) == 0x1;

		// imu event follows when 11101???
		// there are imu-only messages, and imu-after-battery
		bool has_imu      = (*buf & 0x08) == 0x8 && (*buf & 0x10) != 0x10;

		//! @todo: Confirm that messages 4th bit == 1 have no valid
		// imu data that we erroneously drop

		// clang-format on

		buf++;

		if (has_battery) {
			controller_handle_battery(d, *buf++);
		}

		if (has_buttons) {
			controller_handle_buttons(d, *buf++);
		}
		if (has_trigger) {
			controller_handle_analog_trigger(d, *buf++);
		}
		if (has_trackpad) {
			controller_handle_touch_position(d, buf);
			buf += 4;
		}
		if (has_imu) {
			vive_controller_handle_imu_sample(
			    d, (struct vive_imu_report *)buf);
		}
	}

	if (buf > end)
		VIVE_CONTROLLER_ERROR(d, "overshoot: %ld\n", buf - end);

	//! @todo: Parse lighthouse v2 data
}
/*
 * Decodes multiplexed Wireless Receiver messages.
 */
static void
vive_controller_decode_message(struct vive_controller_device *d,
                               struct vive_controller_message *message)
{
	d->last_ticks =
	    (message->timestamp_hi << 24) | (message->timestamp_lo << 16);

	//! @todo: Check if Vive controller on watchman2 is correctly handled
	//! with watchman2 codepath
	switch (d->watchman_gen) {
	case WATCHMAN_GEN1:
		vive_controller_decode_watchmanv1(d, message);
		break;
	case WATCHMAN_GEN2:
		vive_controller_decode_watchmanv2(d, message);
		break;
	default: VIVE_CONTROLLER_ERROR(d, "Can't decode unknown watchman gen");
	}
}

#define FEATURE_BUFFER_SIZE 256

static int
vive_controller_device_update(struct vive_controller_device *d)
{
	uint8_t buf[FEATURE_BUFFER_SIZE];
	do {
		int ret = os_hid_read(d->controller_hid, buf, sizeof(buf), 0);
		if (ret < 0) {
			return ret;
		}
		if (ret == 0) {
			// controller off
			return true;
		}

		if (buf[0] == VIVE_CONTROLLER_REPORT1_ID) {
			struct vive_controller_report1 *pkt =
			    (struct vive_controller_report1 *)buf;
			vive_controller_decode_message(d, &pkt->message);
		} else if (buf[0] == VIVE_CONTROLLER_REPORT2_ID) {
			struct vive_controller_report2 *pkt =
			    (struct vive_controller_report2 *)buf;
			vive_controller_decode_message(d, &pkt->message[0]);
			vive_controller_decode_message(d, &pkt->message[1]);
		} else if (buf[0] == VIVE_CONTROLLER_DISCONNECT_REPORT_ID) {
			VIVE_CONTROLLER_DEBUG(d, "Controller disconnected.");
		} else {
			VIVE_CONTROLLER_ERROR(
			    d, "Unknown controller message type: %u", buf[0]);
		}

	} while (true);

	return 0;
}

static void *
vive_controller_run_thread(void *ptr)
{
	struct vive_controller_device *d = (struct vive_controller_device *)ptr;

	uint8_t buf[FEATURE_BUFFER_SIZE];
	while (os_hid_read(d->controller_hid, buf, sizeof(buf), 0) > 0) {
		// Empty queue first
	}

	os_thread_helper_lock(&d->controller_thread);
	while (os_thread_helper_is_running_locked(&d->controller_thread)) {
		os_thread_helper_unlock(&d->controller_thread);

		if (!vive_controller_device_update(d)) {
			return NULL;
		}

		// Just keep swimming.
		os_thread_helper_lock(&d->controller_thread);
	}

	return NULL;
}

static char *
_json_get_string(const cJSON *json, const char *name)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	return strdup(item->valuestring);
}

static void
print_vec3(const char *title, struct xrt_vec3 *vec)
{
	printf("%s = %f %f %f\n", title, (double)vec->x, (double)vec->y,
	       (double)vec->z);
}

static void
_get_pose_from_pos_x_z(const cJSON *obj, struct xrt_pose *pose)
{
	struct xrt_vec3 plus_x, plus_z;
	u_json_get_vec3(u_json_get(obj, "plus_x"), &plus_x);
	u_json_get_vec3(u_json_get(obj, "plus_z"), &plus_z);
	u_json_get_vec3(u_json_get(obj, "position"), &pose->position);

	math_quat_from_plus_x_z(&plus_x, &plus_z, &pose->orientation);
}

static bool
vive_controller_parse_config(struct vive_controller_device *d,
                             char *json_string)
{
	VIVE_CONTROLLER_DEBUG(d, "JSON config:\n%s\n", json_string);

	cJSON *json = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json)) {
		VIVE_CONTROLLER_ERROR(d, "Could not parse JSON data.");
		return false;
	}

	d->firmware.model_number = _json_get_string(json, "model_number");
	if (strcmp(d->firmware.model_number, "Vive. Controller MV") == 0) {
		d->variant = CONTROLLER_VIVE_WAND;
		VIVE_CONTROLLER_DEBUG(d, "Found Vive Wand controller");
	} else if (strcmp(d->firmware.model_number, "Knuckles Right") == 0) {
		d->variant = CONTROLLER_INDEX_RIGHT;
		VIVE_CONTROLLER_DEBUG(d, "Found Knuckles Right controller");
	} else if (strcmp(d->firmware.model_number, "Knuckles Left") == 0) {
		d->variant = CONTROLLER_INDEX_LEFT;
		VIVE_CONTROLLER_DEBUG(d, "Found Knuckles Left controller");
	} else {
		VIVE_CONTROLLER_ERROR(d, "Failed to parse controller variant");
	}

	switch (d->variant) {
	case CONTROLLER_VIVE_WAND: {
		u_json_get_vec3(u_json_get(json, "acc_bias"), &d->imu.acc_bias);
		u_json_get_vec3(u_json_get(json, "acc_scale"),
		                &d->imu.acc_scale);
		u_json_get_vec3(u_json_get(json, "gyro_bias"),
		                &d->imu.gyro_bias);
		u_json_get_vec3(u_json_get(json, "gyro_scale"),
		                &d->imu.gyro_scale);
		d->firmware.mb_serial_number =
		    _json_get_string(json, "mb_serial_number");
	} break;
	case CONTROLLER_INDEX_LEFT:
	case CONTROLLER_INDEX_RIGHT: {
		const cJSON *imu = u_json_get(json, "imu");
		_get_pose_from_pos_x_z(imu, &d->imu.trackref);

		u_json_get_vec3(u_json_get(imu, "acc_bias"), &d->imu.acc_bias);
		u_json_get_vec3(u_json_get(imu, "acc_scale"),
		                &d->imu.acc_scale);
		u_json_get_vec3(u_json_get(imu, "gyro_bias"),
		                &d->imu.gyro_bias);
	} break;
	default:
		VIVE_CONTROLLER_ERROR(d, "Unknown Vive watchman variant.\n");
		return false;
	}

	d->firmware.device_serial_number =
	    _json_get_string(json, "device_serial_number");

	cJSON_Delete(json);

	// clang-format off
	VIVE_CONTROLLER_DEBUG(d, "= Vive controller configuration =");

	VIVE_CONTROLLER_DEBUG(d, "model_number: %s", d->firmware.model_number);
	VIVE_CONTROLLER_DEBUG(d, "mb_serial_number: %s", d->firmware.mb_serial_number);
	VIVE_CONTROLLER_DEBUG(d, "device_serial_number: %s", d->firmware.device_serial_number);

	if (d->print_debug) {
		print_vec3("acc_bias", &d->imu.acc_bias);
		print_vec3("acc_scale", &d->imu.acc_scale);
		print_vec3("gyro_bias", &d->imu.gyro_bias);
		print_vec3("gyro_scale", &d->imu.gyro_scale);
	}

	// clang-format on

	return true;
}

/*
 *
 * Prober functions.
 *
 */
#define SET_WAND_INPUT(NAME, NAME2)                                            \
	do {                                                                   \
		(d->base.inputs[VIVE_CONTROLLER_INDEX_##NAME].name =           \
		     XRT_INPUT_VIVE_##NAME2);                                  \
	} while (0)

#define SET_INDEX_INPUT(NAME, NAME2)                                           \
	do {                                                                   \
		(d->base.inputs[VIVE_CONTROLLER_INDEX_##NAME].name =           \
		     XRT_INPUT_INDEX_##NAME2);                                 \
	} while (0)
int
vive_controller_found(struct xrt_prober *xp,
                      struct xrt_prober_device **devices,
                      size_t num_devices,
                      size_t index,
                      cJSON *attached_data,
                      struct xrt_device **out_xdevs)
{
	struct xrt_prober_device *dev = devices[index];
	int ret;

	static int controller_num = 0;

	struct os_hid_device *controller_hid = NULL;
	ret = xp->open_hid_interface(xp, dev, 0, &controller_hid);
	if (ret != 0) {
		return -1;
	}

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	struct vive_controller_device *d = U_DEVICE_ALLOCATE(
	    struct vive_controller_device, flags, VIVE_CONTROLLER_MAX_INDEX, 1);

	d->watchman_gen = WATCHMAN_GEN_UNKNOWN;
	d->variant = CONTROLLER_UNKNOWN;

	if (dev->vendor_id == VALVE_VID &&
	    dev->product_id == VIVE_WATCHMAN_DONGLE) {
		d->watchman_gen = WATCHMAN_GEN1;
		VIVE_CONTROLLER_DEBUG(d, "Found watchman gen 1");
	} else if (dev->vendor_id == VALVE_VID &&
	           dev->product_id == VIVE_WATCHMAN_DONGLE_GEN2) {
		d->watchman_gen = WATCHMAN_GEN2;
		VIVE_CONTROLLER_DEBUG(d, "Found watchman gen 2");
	} else {
		VIVE_CONTROLLER_ERROR(d, "Unknown watchman gen");
	}

	m_imu_3dof_init(&d->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	/* default values, will be queried from device */
	d->imu.gyro_range = 8.726646f;
	d->imu.acc_range = 39.226600f;

	d->imu.acc_scale.x = 1.0f;
	d->imu.acc_scale.y = 1.0f;
	d->imu.acc_scale.z = 1.0f;
	d->imu.gyro_scale.x = 1.0f;
	d->imu.gyro_scale.y = 1.0f;
	d->imu.gyro_scale.z = 1.0f;

	d->imu.acc_bias.x = 0.0f;
	d->imu.acc_bias.y = 0.0f;
	d->imu.acc_bias.z = 0.0f;
	d->imu.gyro_bias.x = 0.0f;
	d->imu.gyro_bias.y = 0.0f;
	d->imu.gyro_bias.z = 0.0f;

	d->print_spew = debug_get_bool_option_vive_controller_spew();
	d->print_debug = debug_get_bool_option_vive_controller_debug();

	d->controller_hid = controller_hid;

	d->base.destroy = vive_controller_device_destroy;
	d->base.get_tracked_pose = vive_controller_device_get_tracked_pose;
	d->base.set_output = vive_controller_device_set_output;

	snprintf(d->base.str, XRT_DEVICE_NAME_LEN, "%s %i", "Vive Controller",
	         (int)(controller_num));

	d->index = controller_num;

	//! @todo: reading range report fails for powered off controller
	if (vive_get_imu_range_report(d->controller_hid, &d->imu.gyro_range,
	                              &d->imu.acc_range) != 0) {
		VIVE_CONTROLLER_ERROR(
		    d, "Could not get watchman IMU range packet!");
		free(d);
		return 0;
	}

	VIVE_CONTROLLER_DEBUG(d, "Vive controller gyroscope range     %f",
	                      d->imu.gyro_range);
	VIVE_CONTROLLER_DEBUG(d, "Vive controller accelerometer range %f",
	                      d->imu.acc_range);

	// successful config parsing determines d->variant
	char *config = vive_read_config(d->controller_hid);
	if (config != NULL) {
		vive_controller_parse_config(d, config);
		free(config);
	} else {
		VIVE_CONTROLLER_ERROR(d,
		                      "Could not get Vive controller config\n");
		free(d);
		return 0;
	}

	if (d->variant == CONTROLLER_VIVE_WAND) {
		d->base.name = XRT_DEVICE_VIVE_WAND;

		SET_WAND_INPUT(SYSTEM_CLICK, SYSTEM_CLICK);
		SET_WAND_INPUT(SQUEEZE_CLICK, SQUEEZE_CLICK);
		SET_WAND_INPUT(MENU_CLICK, MENU_CLICK);
		SET_WAND_INPUT(TRIGGER_CLICK, TRIGGER_CLICK);
		SET_WAND_INPUT(TRIGGER_VALUE, TRIGGER_VALUE);
		SET_WAND_INPUT(TRACKPAD_X, TRACKPAD_X);
		SET_WAND_INPUT(TRACKPAD_Y, TRACKPAD_Y);
		SET_WAND_INPUT(TRACKPAD_CLICK, TRACKPAD_CLICK);
		SET_WAND_INPUT(TRACKPAD_TOUCH, TRACKPAD_TOUCH);

		SET_WAND_INPUT(AIM_POSE, AIM_POSE);
		SET_WAND_INPUT(GRIP_POSE, GRIP_POSE);

		d->base.outputs[0].name = XRT_OUTPUT_NAME_VIVE_HAPTIC;

		d->base.update_inputs =
		    vive_controller_device_update_wand_inputs;
	} else if (d->variant == CONTROLLER_INDEX_LEFT ||
	           d->variant == CONTROLLER_INDEX_RIGHT) {
		d->base.name = XRT_DEVICE_INDEX_CONTROLLER;

		SET_INDEX_INPUT(SYSTEM_CLICK, SYSTEM_CLICK);
		SET_INDEX_INPUT(A_CLICK, A_CLICK);
		SET_INDEX_INPUT(B_CLICK, B_CLICK);
		SET_INDEX_INPUT(TRIGGER_CLICK, TRIGGER_CLICK);
		SET_INDEX_INPUT(TRIGGER_VALUE, TRIGGER_VALUE);
		SET_INDEX_INPUT(TRACKPAD_X, TRACKPAD_X);
		SET_INDEX_INPUT(TRACKPAD_Y, TRACKPAD_Y);
		SET_INDEX_INPUT(TRACKPAD_TOUCH, TRACKPAD_TOUCH);
		SET_INDEX_INPUT(THUMBSTICK_X, THUMBSTICK_X);
		SET_INDEX_INPUT(THUMBSTICK_Y, THUMBSTICK_Y);
		SET_INDEX_INPUT(THUMBSTICK_CLICK, THUMBSTICK_CLICK);

		SET_INDEX_INPUT(AIM_POSE, AIM_POSE);
		SET_INDEX_INPUT(GRIP_POSE, GRIP_POSE);

		d->base.outputs[0].name = XRT_OUTPUT_NAME_INDEX_HAPTIC;

		d->base.update_inputs =
		    vive_controller_device_update_index_inputs;
	} else {
		d->base.name = XRT_DEVICE_GENERIC_HMD;
		VIVE_CONTROLLER_ERROR(d,
		                      "Failed to assign update input function");
	}

	if (d->controller_hid) {
		ret = os_thread_helper_start(&d->controller_thread,
		                             vive_controller_run_thread, d);
		if (ret != 0) {
			VIVE_CONTROLLER_ERROR(
			    d, "Failed to start mainboard thread!");
			vive_controller_device_destroy((struct xrt_device *)d);
			return 0;
		}
	}

	out_xdevs[0] = &(d->base);
	VIVE_CONTROLLER_DEBUG(d, "Opened vive controller!\n");

	controller_num++;
	return 1;
}
