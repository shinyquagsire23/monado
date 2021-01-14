// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PlayStation Move motion controller prober and driver code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_psmv
 */

#include "xrt/xrt_prober.h"
#include "xrt/xrt_tracking.h"

#include "os/os_threading.h"
#include "os/os_hid.h"
#include "os/os_time.h"

#include "math/m_api.h"
#include "math/m_imu_pre.h"
#include "math/m_space.h"

#include "tracking/t_imu.h"

#include "util/u_var.h"
#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_logging.h"

#include "psmv_interface.h"

#include "math/m_mathinclude.h"

#include <stdio.h>
#include <assert.h>


/*!
 * @ingroup drv_psmv
 * @{
 */


/*
 *
 * Defines & structs.
 *
 */

#define PSMV_BALL_DIAMETER_M 0.045  // 45 mm
#define PSMV_BALL_FROM_IMU_Y_M 0.09 // 9 cm

// clang-format off
#define PSMV_TRACE(p, ...) U_LOG_XDEV_IFL_T(&p->base, p->log_level, __VA_ARGS__)
#define PSMV_DEBUG(p, ...) U_LOG_XDEV_IFL_D(&p->base, p->log_level, __VA_ARGS__)
#define PSMV_ERROR(p, ...) U_LOG_XDEV_IFL_E(&p->base, p->log_level, __VA_ARGS__)
// clang-format on

DEBUG_GET_ONCE_LOG_OPTION(psmv_log, "PSMV_LOG", U_LOGGING_WARN)

/*!
 * Indices where each input is in the input list.
 */
enum psmv_input_index
{
	PSMV_INDEX_PS_CLICK,
	PSMV_INDEX_MOVE_CLICK,
	PSMV_INDEX_START_CLICK,
	PSMV_INDEX_SELECT_CLICK,
	PSMV_INDEX_SQUARE_CLICK,
	PSMV_INDEX_CROSS_CLICK,
	PSMV_INDEX_CIRCLE_CLICK,
	PSMV_INDEX_TRIANGLE_CLICK,
	PSMV_INDEX_TRIGGER_VALUE,
	PSMV_INDEX_GRIP_POSE,
	PSMV_INDEX_AIM_POSE,
	PSMV_INDEX_BODY_CENTER_POSE,
	PSMV_INDEX_BALL_CENTER_POSE,
};

/*!
 * Mask for the button in the button uint32_t.
 */
enum psmv_button_bit
{
	// clang-format off
	PSMV_BUTTON_BIT_MOVE_F2     = (1 << 6),
	PSMV_BUTTON_BIT_TRIGGER_F2  = (1 << 7),

	PSMV_BUTTON_BIT_PS          = (1 << 8),

	PSMV_BUTTON_BIT_MOVE_F1     = (1 << 11),
	PSMV_BUTTON_BIT_TRIGGER_F1  = (1 << 12),

	PSMV_BUTTON_BIT_TRIANGLE    = (1 << 20),
	PSMV_BUTTON_BIT_CIRCLE      = (1 << 21),
	PSMV_BUTTON_BIT_CROSS       = (1 << 22),
	PSMV_BUTTON_BIT_SQUARE      = (1 << 23),

	PSMV_BUTTON_BIT_START       = (1 << 27),
	PSMV_BUTTON_BIT_SELECT      = (1 << 24),

	PSMV_BUTTON_BIT_MOVE_ANY    = PSMV_BUTTON_BIT_MOVE_F1 |
	                              PSMV_BUTTON_BIT_MOVE_F2,
	PSMV_BUTTON_BIT_TRIGGER_ANY = PSMV_BUTTON_BIT_TRIGGER_F1 |
	                              PSMV_BUTTON_BIT_TRIGGER_F2,
	// clang-format on
};

/*!
 * Led setting packet.
 */
struct psmv_set_led
{
	uint8_t id;
	uint8_t zero;
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t _unknown;
	uint8_t rumble;
	uint8_t _pad[49 - 7];
};

/*!
 * Wire encoding of a single 32 bit float, "little" endian.
 */
struct psmv_f32_wire
{
	uint8_t val[4];
};

/*!
 * Wire encoding of three 32 bit float, "little" endian.
 */
struct psmv_vec3_f32_wire
{
	struct psmv_f32_wire x;
	struct psmv_f32_wire y;
	struct psmv_f32_wire z;
};

/*!
 * Wire encoding of a single 16 bit integer, little endian.
 *
 * The values are unsigned 16-bit integers and stored as two's complement. The
 * values are shifted up to always report positive numbers. Subtract 0x8000 to
 * obtain signed values and determine direction from the sign.
 */
struct psmv_u16_wire
{
	uint8_t low;
	uint8_t high;
};

/*!
 * Wire encoding of three 16 bit integers, little endian.
 *
 * The values are unsigned 16-bit integers and stored as two's complement. The
 * values are shifted up to always report positive numbers. Subtract 0x8000 to
 * obtain signed values and determine direction from the sign.
 */
struct psmv_vec3_u16_wire
{
	struct psmv_u16_wire x;
	struct psmv_u16_wire y;
	struct psmv_u16_wire z;
};

/*!
 * Wire encoding of a single 16 bit integer, little endian.
 */
struct psmv_i16_wire
{
	uint8_t low;
	uint8_t high;
};

/*!
 * Wire encoding of three 16 bit integers, little endian.
 *
 * The values are signed 16-bit integers and stored as two's complement.
 */
struct psmv_vec3_i16_wire
{
	struct psmv_i16_wire x;
	struct psmv_i16_wire y;
	struct psmv_i16_wire z;
};

/*!
 * Part of a calibration data, multiple packets make up a single data packet.
 */
struct psmv_calibration_part
{
	uint8_t id;
	uint8_t which;
	uint8_t data[49 - 2];
};

/*!
 * Calibration data, multiple packets goes into this.
 */
struct psmv_calibration_zcm1
{
	uint8_t id;
	uint8_t which;
	uint8_t _pad0[2];
	struct psmv_vec3_u16_wire accel_max_z;
	struct psmv_vec3_u16_wire accel_min_x;
	struct psmv_vec3_u16_wire accel_min_z;
	struct psmv_vec3_u16_wire accel_max_x;
	struct psmv_vec3_u16_wire accel_max_y;
	struct psmv_vec3_u16_wire accel_min_y;
	uint8_t _pad1[2];
	struct psmv_vec3_u16_wire gyro_bias_0;
	uint8_t _pad2[2];
	struct psmv_vec3_u16_wire gyro_bias_1;
	uint8_t _pad3[7];
	uint8_t _pad4;
	uint8_t _pad5[2];
	uint8_t _pad6[2];
	uint8_t _pad7[2];
	struct psmv_vec3_u16_wire gyro_rot_x;
	uint8_t _pad8[2];
	struct psmv_vec3_u16_wire gyro_rot_y;
	uint8_t _pad9[2];
	struct psmv_vec3_u16_wire gyro_rot_z;
	uint8_t _pad10[2];
	struct psmv_vec3_f32_wire unknown_vec3;
	struct psmv_vec3_f32_wire gyro_fact;
	struct psmv_f32_wire unknown_float_0;
	struct psmv_f32_wire unknown_float_1;
	uint8_t _pad[17];
};

/*!
 * Parsed calibration data from a ZCM1 device.
 */
struct psmv_parsed_calibration_zcm1
{
	struct xrt_vec3_i32 accel_min_x;
	struct xrt_vec3_i32 accel_max_x;
	struct xrt_vec3_i32 accel_min_y;
	struct xrt_vec3_i32 accel_max_y;
	struct xrt_vec3_i32 accel_min_z;
	struct xrt_vec3_i32 accel_max_z;

	/*!
	 * From: https://github.com/nitsch/moveonpc/wiki/Calibration-data
	 *
	 * Coded as the one before. The values are very near to 1.0.
	 *
	 * I observed, that when I multiply this vector with the gyro bias
	 * vector before subtracting from the gyro 80rpm measures, I get a
	 * better calibration.
	 *
	 * So in order to get the accurate 80rpm measures:
	 * GyroMeasure80rpm-(GyroBias1*UnknownVector2) or
	 * GyroMeasure80rpm-(GyroBias2*UnknownVector2)
	 */
	struct xrt_vec3 gyro_fact;
	struct xrt_vec3_i32 gyro_bias_0;
	struct xrt_vec3_i32 gyro_bias_1;
	struct xrt_vec3_i32 gyro_rot_x;
	struct xrt_vec3_i32 gyro_rot_y;
	struct xrt_vec3_i32 gyro_rot_z;

	struct xrt_vec3 unknown_vec3;
	float unknown_float_0, unknown_float_1;
};

/*!
 * Calibration data, multiple packets goes into this.
 */
struct psmv_calibration_zcm2
{
	uint8_t id;
	uint8_t which;
	struct psmv_vec3_i16_wire accel_max_x;
	struct psmv_vec3_i16_wire accel_min_x;
	struct psmv_vec3_i16_wire accel_max_y;
	struct psmv_vec3_i16_wire accel_min_y;
	struct psmv_vec3_i16_wire accel_max_z;
	struct psmv_vec3_i16_wire accel_min_z;
	//! Pretty sure this is gryo bias.
	struct psmv_vec3_i16_wire gyro_bias;
	uint8_t _pad0[4];
	struct psmv_vec3_i16_wire gyro_pos_x;
	struct psmv_vec3_i16_wire gyro_pos_y;
	struct psmv_vec3_i16_wire gyro_pos_z;
	struct psmv_vec3_i16_wire gyro_neg_x;
	struct psmv_vec3_i16_wire gyro_neg_y;
	struct psmv_vec3_i16_wire gyro_neg_z;
	uint8_t _pad1[12];
};


/*!
 * Parsed calibration data from a ZCM2 device.
 */
struct psmv_parsed_calibration_zcm2
{
	struct xrt_vec3_i32 accel_min_x;
	struct xrt_vec3_i32 accel_max_x;
	struct xrt_vec3_i32 accel_min_y;
	struct xrt_vec3_i32 accel_max_y;
	struct xrt_vec3_i32 accel_min_z;
	struct xrt_vec3_i32 accel_max_z;

	struct xrt_vec3_i32 gyro_neg_x;
	struct xrt_vec3_i32 gyro_pos_x;
	struct xrt_vec3_i32 gyro_neg_y;
	struct xrt_vec3_i32 gyro_pos_y;
	struct xrt_vec3_i32 gyro_neg_z;
	struct xrt_vec3_i32 gyro_pos_z;

	//! Pretty sure this is gryo bias.
	struct xrt_vec3_i32 gyro_bias;
};

/*!
 * Input package for ZCM1.
 */
struct psmv_input_zcm1
{
	uint8_t header;
	uint8_t buttons[4];
	uint8_t trigger_f1;
	uint8_t trigger_f2;
	uint8_t unknown[4];
	uint8_t timestamp_high;
	uint8_t battery;
	struct psmv_vec3_u16_wire accel_f1;
	struct psmv_vec3_u16_wire accel_f2;
	struct psmv_vec3_u16_wire gyro_f1;
	struct psmv_vec3_u16_wire gyro_f2;
	uint8_t temp_mag[6];
	uint8_t timestamp_low;
	uint8_t pad[49 - 44];
};

/*!
 * Input package for ZCM2.
 */
struct psmv_input_zcm2
{
	uint8_t header;
	uint8_t buttons[4];
	uint8_t trigger;
	uint8_t trigger_low_pass;
	uint8_t pad0[4];
	uint8_t timestamp_high_copy;
	uint8_t battery;
	struct psmv_vec3_i16_wire accel;
	struct psmv_vec3_i16_wire accel_copy;
	struct psmv_vec3_i16_wire gyro;
	struct psmv_vec3_i16_wire gyro_copy;
	uint8_t temp[2];
	uint8_t timestamp_low;
	uint8_t timestamp_high;
	uint8_t pad1[2];
	uint8_t timestamp_low_copy;
};

/*!
 * A parsed sample of accel and gyro.
 */
struct psmv_parsed_sample
{
	struct xrt_vec3_i32 accel;
	struct xrt_vec3_i32 gyro;
};

/*!
 * A parsed input packet.
 */
struct psmv_parsed_input
{
	uint32_t buttons;
	uint16_t timestamp;
	uint16_t timestamp_copy;
	uint8_t battery;
	uint8_t seq_no;


	union {
		//! Trigger for the last two frames (ZCM1).
		uint8_t trigger_values[2];

		struct
		{
			//! Low-pass filtered version of trigger (ZCM2).
			uint8_t trigger_low_pass;

			//! Trigger (ZCM2).
			uint8_t trigger;
		};
	};

	union {
		//! Accelerometer and gyro scope samples (ZCM1).
		struct psmv_parsed_sample samples[2];

		struct
		{
			//! Accelerometer and gyro scope samples (ZCM2).
			struct psmv_parsed_sample sample;

			//! Copy of above (ZCM2).
			struct psmv_parsed_sample sample_copy;
		};
	};
};

/*!
 * A single PlayStation Move Controller.
 *
 * A note about coordinate system. If you stand the controller in front of you
 * so that the ball is pointing upward, buttons towards you. Then think of the
 * ball as a head that is looking away from you. The buttons then are is it's
 * back, the trigger the front.
 *
 * Translated to axis that means the ball is on the Y+ axis, the buttons on the
 * Z+ axis, the trigger on the Z- axis, the USB port on the Y- axis, the start
 * button on the X+ axis, select button on the X- axis.
 *
 * @implements xrt_device
 */
struct psmv_device
{
	struct xrt_device base;

	struct os_hid_device *hid;

	struct xrt_tracked_psmv *ball;

	struct os_thread_helper oth;

	struct
	{
		int64_t resend_time;
		struct xrt_colour_rgb_u8 led;
		uint8_t rumble;
	} wants; //!< What should be set.

	struct
	{
		struct xrt_colour_rgb_u8 led;
		uint8_t rumble;
	} state; //!< What is currently set on the device.

	struct
	{
		union {
			struct psmv_parsed_calibration_zcm1 zcm1;
			struct psmv_parsed_calibration_zcm2 zcm2;
		};

		struct m_imu_pre_filter prefilter;
	} calibration;


	struct
	{
		//! Lock for last and fusion.
		struct os_mutex lock;

		//! Last sensor read.
		struct psmv_parsed_input last;

		//! Last time we got a package.
		uint64_t last_timestamp_ns;

		struct
		{
			struct xrt_quat rot;
			struct xrt_vec3 rotvec;
			struct xrt_vec3 angvel;
			struct imu_fusion *fusion;
			struct
			{
				struct xrt_vec3 accel;
				struct xrt_vec3 gyro;
			} variance;
		} fusion;
	};


	struct
	{
		//! Last adjusted accelerator value.
		struct xrt_vec3 accel;
		//! Last adjusted gyro value.
		struct xrt_vec3 gyro;
	} read;

	// Product ID used to tell the difference between ZCM1 and ZCM2.
	uint16_t pid;

	enum u_logging_level log_level;

	struct
	{
		bool control;
		bool calibration;
		bool last_frame;
		bool fusion;
	} gui;
};


/*
 *
 * Pre-declare some functions.
 *
 */

static int
psmv_get_calibration(struct psmv_device *psmv);

static int
psmv_parse_input(struct psmv_device *psmv, void *data, struct psmv_parsed_input *input);


/*
 *
 * Smaller helper functions.
 *
 */

static inline struct psmv_device *
psmv_device(struct xrt_device *xdev)
{
	return (struct psmv_device *)xdev;
}

static uint32_t
psmv_calc_delta_and_handle_rollover(uint32_t next, uint32_t last)
{
	uint32_t tick_delta = next - last;

	// The 16-bit tick counter has rolled over,
	// adjust the "negative" value to be positive.
	if (tick_delta > 0xffff) {
		tick_delta += 0x10000;
	}

	return tick_delta;
}

static inline uint8_t
psmv_clamp_zero_to_one_float_to_u8(float v)
{
	float vf = v * 255.0f;

	if (vf >= 255.0f) {
		return 0xff;
	}
	if (vf >= 0.0f) {
		return (uint8_t)vf;
	}
	return 0x00;
}

static void
psmv_update_input_click(struct psmv_device *psmv, int index, int64_t when_ns, uint32_t bit)
{
	psmv->base.inputs[index].timestamp = when_ns;
	psmv->base.inputs[index].value.boolean = (psmv->last.buttons & bit) != 0;
}

static void
psmv_update_trigger_value(struct psmv_device *psmv, int index, int64_t when_ns)
{
	psmv->base.inputs[index].timestamp = when_ns;
	psmv->base.inputs[index].value.vec1.x = psmv->last.trigger / 255.0f;
}


/*
 *
 * Internal functions.
 *
 */

/*!
 * Does the actual sending of the led control package to the device.
 */
static int
psmv_send_led_control_locked(
    volatile struct psmv_device *psmv, uint8_t red, uint8_t green, uint8_t blue, uint8_t rumble)
{
	struct psmv_set_led msg;
	U_ZERO(&msg);
	msg.id = 0x06;
	msg.red = red;
	msg.green = green;
	msg.blue = blue;
	msg.rumble = rumble;

	return os_hid_write(psmv->hid, (uint8_t *)&msg, sizeof(msg));
}

static void
psmv_led_and_trigger_update_locked(volatile struct psmv_device *psmv, int64_t time)
{
	// Need to keep sending led control packets to keep the leds on.
	if (psmv->wants.resend_time > time && psmv->state.led.r == psmv->wants.led.r &&
	    psmv->state.led.g == psmv->wants.led.g && psmv->state.led.b == psmv->wants.led.b &&
	    psmv->state.rumble == psmv->wants.rumble) {
		return;
	}

	psmv->state.led.r = psmv->wants.led.r;
	psmv->state.led.g = psmv->wants.led.g;
	psmv->state.led.b = psmv->wants.led.b;
	psmv->state.rumble = psmv->wants.rumble;

	psmv->wants.resend_time = time + 1000000000;
	psmv_send_led_control_locked(psmv, psmv->state.led.r, psmv->state.led.g, psmv->state.led.b, psmv->state.rumble);
}

static void
psmv_led_and_trigger_update(struct psmv_device *psmv, int64_t time)
{
	os_mutex_lock(&psmv->lock);
	psmv_led_and_trigger_update_locked(psmv, time);
	os_mutex_unlock(&psmv->lock);
}

static void
update_fusion(struct psmv_device *psmv,
              struct psmv_parsed_sample *sample,
              timepoint_ns timestamp_ns,
              time_duration_ns delta_ns)
{
	struct xrt_vec3 mag = {0.0f, 0.0f, 0.0f};

	(void)mag;


	struct xrt_vec3_i32 *ra = &sample->accel;
	struct xrt_vec3_i32 *rg = &sample->gyro;

	m_imu_pre_filter_data(&psmv->calibration.prefilter, ra, rg, &psmv->read.accel, &psmv->read.gyro);

	if (psmv->ball != NULL) {
		// We have positional tracking
		struct xrt_tracking_sample sample;
		sample.accel_m_s2 = psmv->read.accel;
		sample.gyro_rad_secs = psmv->read.gyro;

		xrt_tracked_psmv_push_imu(psmv->ball, timestamp_ns, &sample);
	} else {
		// Orientation-only tracking

#if 0
		// Super simple fusion.
		math_quat_integrate_velocity(
		    &psmv->fusion.rot, &psmv->read.gyro, dt, &psmv->fusion.rot);
#else
		imu_fusion_incorporate_gyros_and_accelerometer(psmv->fusion.fusion, timestamp_ns, &psmv->read.gyro,
		                                               &psmv->fusion.variance.gyro, &psmv->read.accel,
		                                               &psmv->fusion.variance.accel, NULL);
		imu_fusion_get_prediction(psmv->fusion.fusion, timestamp_ns, &psmv->fusion.rot, &psmv->fusion.angvel);
		imu_fusion_get_prediction_rotation_vec(psmv->fusion.fusion, timestamp_ns, &psmv->fusion.rotvec);
#endif
	}
}

/*!
 * Reads one packet from the device, handles time out, locking and checking if
 * the thread has been told to shut down.
 */
static bool
psmv_read_one_packet(struct psmv_device *psmv, uint8_t *buffer, size_t size)
{
	os_thread_helper_lock(&psmv->oth);

	while (os_thread_helper_is_running_locked(&psmv->oth)) {

		os_thread_helper_unlock(&psmv->oth);

		int ret = os_hid_read(psmv->hid, buffer, size, 1000);

		if (ret == 0) {
			PSMV_DEBUG(psmv, "Timeout");

			// Must lock thread before check in while.
			os_thread_helper_lock(&psmv->oth);
			continue;
		}
		if (ret < 0) {
			PSMV_ERROR(psmv, "Failed to read device '%i'!", ret);
			return false;
		}

		return true;
	}

	return false;
}

static void *
psmv_run_thread(void *ptr)
{
	struct psmv_device *psmv = (struct psmv_device *)ptr;

	union {
		uint8_t buffer[256];
		struct psmv_input_zcm1 input;
	} data;

	struct psmv_parsed_input input = {0};

	while (os_hid_read(psmv->hid, data.buffer, sizeof(data), 0) > 0) {
		// Empty queue first
	}

	// Now wait for a package to sync up, it's discarded but that's okay.
	if (!psmv_read_one_packet(psmv, data.buffer, sizeof(data))) {
		return NULL;
	}

	timepoint_ns then_ns = os_monotonic_get_ns();

	while (psmv_read_one_packet(psmv, data.buffer, sizeof(data))) {

		timepoint_ns now_ns = os_monotonic_get_ns();

		int num = psmv_parse_input(psmv, data.buffer, &input);

		time_duration_ns delta_ns = now_ns - then_ns;
		then_ns = now_ns;

		// Lock last and the fusion.
		os_mutex_lock(&psmv->lock);

		// Make sure the leds stays on.
		psmv_led_and_trigger_update_locked(psmv, now_ns);

		// Copy to device.
		psmv->last = input;

		// Process the parsed data.
		if (num == 2) {
			// ZCM1
			update_fusion(psmv, &input.samples[0], now_ns - (delta_ns / 2.0), (delta_ns / 2.0));
			update_fusion(psmv, &input.samples[1], now_ns, (delta_ns / 2.0));
			psmv->last_timestamp_ns = now_ns;
		} else if (num == 1) {
			// ZCM2
			update_fusion(psmv, &input.sample, now_ns, delta_ns);
			psmv->last_timestamp_ns = now_ns;
		} else {
			assert(false);
		}

		// Now done.
		os_mutex_unlock(&psmv->lock);
	}

	return NULL;
}

static void
psmv_get_fusion_pose(struct psmv_device *psmv,
                     enum xrt_input_name name,
                     timepoint_ns when,
                     struct xrt_space_relation *out_relation)
{
	out_relation->pose.orientation = psmv->fusion.rot;
	out_relation->angular_velocity = psmv->fusion.angvel;
	out_relation->linear_velocity.x = 0.0f;
	out_relation->linear_velocity.y = 0.0f;
	out_relation->linear_velocity.z = 0.0f;

	/*!
	 * @todo This is hack, fusion reports angvel relative to the device but
	 * it needs to be in relation to the base space. Rotating it with the
	 * device orientation is enough to get it into the right space, angular
	 * velocity is a derivative so needs a special rotation.
	 */
	math_quat_rotate_derivative(&psmv->fusion.rot, &psmv->fusion.angvel, &out_relation->angular_velocity);

	//! @todo assuming that orientation is actually currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT | XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT);
}

static void
psmv_add_pose_offset(enum xrt_input_name name, struct xrt_space_graph *xsg)
{
	/*
	 * Both the grip and aim pose needs adjustments, the grip is a rotated
	 * body center pose, while the aim pose needs to rotated and translated
	 * to the tip of the ball.
	 */
	if (name != XRT_INPUT_PSMV_AIM_POSE && name != XRT_INPUT_PSMV_GRIP_POSE) {
		return;
	}

	float y = 0.0;
	if (name == XRT_INPUT_PSMV_AIM_POSE) {
		y += PSMV_BALL_FROM_IMU_Y_M;
		y += PSMV_BALL_DIAMETER_M / 2.0;
	}

	struct xrt_pose pose = {
	    {0.7071068, 0, 0, 0.7071068},
	    {0, y, 0},
	};

	m_space_graph_add_pose(xsg, &pose);
}


/*
 *
 * Device functions.
 *
 */

static void
psmv_device_destroy(struct xrt_device *xdev)
{
	struct psmv_device *psmv = psmv_device(xdev);

	// Destroy the thread object.
	os_thread_helper_destroy(&psmv->oth);

	// Now that the thread is not running we can destroy the lock.
	os_mutex_destroy(&psmv->lock);

	// Destroy the IMU fusion.
	imu_fusion_destroy(psmv->fusion.fusion);

	// Remove the variable tracking.
	u_var_remove_root(psmv);

	// Includes null check, and sets to null.
	xrt_tracked_psmv_destroy(&psmv->ball);

	if (psmv->hid != NULL) {
		psmv_send_led_control_locked(psmv, 0x00, 0x00, 0x00, 0x00);

		os_hid_destroy(psmv->hid);
		psmv->hid = NULL;
	}

	free(psmv);
}

static void
psmv_device_update_inputs(struct xrt_device *xdev)
{
	struct psmv_device *psmv = psmv_device(xdev);

	// Lock the data.
	os_mutex_lock(&psmv->lock);

	// clang-format off
	uint64_t last_ns = psmv->last_timestamp_ns;
	psmv_update_input_click(psmv, PSMV_INDEX_PS_CLICK, last_ns, PSMV_BUTTON_BIT_PS);
	psmv_update_input_click(psmv, PSMV_INDEX_MOVE_CLICK, last_ns, PSMV_BUTTON_BIT_MOVE_ANY);
	psmv_update_input_click(psmv, PSMV_INDEX_START_CLICK, last_ns, PSMV_BUTTON_BIT_START);
	psmv_update_input_click(psmv, PSMV_INDEX_SELECT_CLICK, last_ns, PSMV_BUTTON_BIT_SELECT);
	psmv_update_input_click(psmv, PSMV_INDEX_SQUARE_CLICK, last_ns, PSMV_BUTTON_BIT_SQUARE);
	psmv_update_input_click(psmv, PSMV_INDEX_CROSS_CLICK, last_ns, PSMV_BUTTON_BIT_CROSS);
	psmv_update_input_click(psmv, PSMV_INDEX_CIRCLE_CLICK, last_ns, PSMV_BUTTON_BIT_CIRCLE);
	psmv_update_input_click(psmv, PSMV_INDEX_TRIANGLE_CLICK, last_ns, PSMV_BUTTON_BIT_TRIANGLE);
	psmv_update_trigger_value(psmv, PSMV_INDEX_TRIGGER_VALUE, last_ns);

	// Only report the ball as active if we can track it.
	psmv->base.inputs[PSMV_INDEX_BALL_CENTER_POSE].active = psmv->ball != NULL;
	// clang-format on

	// Done now.
	os_mutex_unlock(&psmv->lock);
}

static xrt_result_t
psmv_device_get_space_graph(struct xrt_device *xdev,
                            enum xrt_input_name name,
                            uint64_t at_timestamp_ns,
                            struct xrt_space_graph *xgs)
{
	struct psmv_device *psmv = psmv_device(xdev);

	psmv_add_pose_offset(name, xgs);

	struct xrt_space_relation *rel = m_space_graph_reserve(xgs);

	if (psmv->ball != NULL) {
		xrt_tracked_psmv_get_tracked_pose(psmv->ball, name, at_timestamp_ns, rel);
	} else {
		psmv_get_fusion_pose(psmv, name, at_timestamp_ns, rel);
	}

	return XRT_SUCCESS;
}

static void
psmv_device_get_tracked_pose(struct xrt_device *xdev,
                             enum xrt_input_name name,
                             uint64_t at_timestamp_ns,
                             struct xrt_space_relation *out_relation)
{
	struct xrt_space_graph xgs = {0};

	psmv_device_get_space_graph(xdev, name, at_timestamp_ns, &xgs);

	m_space_graph_resolve(&xgs, out_relation);
}

static float
amp_scale(struct psmv_device *psmv, float amp)
{
	float min = 0.0;
	float max = 1.0;

	if (psmv->pid == PSMV_PID_ZCM1) {
		// motor does not rumble below 0.25
		min = 0.25;
	}

	return amp * (max - min) + min;
}

static void
psmv_device_set_output(struct xrt_device *xdev, enum xrt_output_name name, union xrt_output_value *value)
{
	struct psmv_device *psmv = psmv_device(xdev);

	if (name != XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION) {
		return;
	}

	os_mutex_lock(&psmv->lock);

	float amp = value->vibration.amplitude;
	// don't scale amp = 0, it disables rumble
	if (amp > 0) {
		amp = amp_scale(psmv, value->vibration.amplitude);
	}
	psmv->wants.rumble = psmv_clamp_zero_to_one_float_to_u8(amp);

	// Resend if the rumble has been changed.
	int64_t now = os_monotonic_get_ns();
	psmv_led_and_trigger_update_locked(psmv, now);

	os_mutex_unlock(&psmv->lock);
}


/*
 *
 * Bindings
 *
 */

static struct xrt_binding_input_pair simple_inputs[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_PSMV_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_PSMV_MOVE_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_PSMV_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_PSMV_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION},
};

static struct xrt_binding_profile binding_profiles[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs,
        .num_inputs = ARRAY_SIZE(simple_inputs),
        .outputs = simple_outputs,
        .num_outputs = ARRAY_SIZE(simple_outputs),
    },
};


/*
 *
 * Prober functions.
 *
 */

#define SET_INPUT(NAME) (psmv->base.inputs[PSMV_INDEX_##NAME].name = XRT_INPUT_PSMV_##NAME)

int
psmv_found(struct xrt_prober *xp,
           struct xrt_prober_device **devices,
           size_t num_devices,
           size_t index,
           cJSON *attached_data,
           struct xrt_device **out_xdevs)
{
	struct os_hid_device *hid = NULL;
	int ret;

	// We do not receive any sensor packages on USB.
	if (devices[index]->bus != XRT_BUS_TYPE_BLUETOOTH) {
		return 0;
	}

	// Sanity check for device type.
	switch (devices[index]->product_id) {
	case PSMV_PID_ZCM1: break;
	case PSMV_PID_ZCM2: break;
	default: return -1;
	}

	ret = xrt_prober_open_hid_interface(xp, devices[index], 0, &hid);
	if (ret != 0) {
		return -1;
	}

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	struct psmv_device *psmv = U_DEVICE_ALLOCATE(struct psmv_device, flags, 13, 1);
	psmv->base.destroy = psmv_device_destroy;
	psmv->base.update_inputs = psmv_device_update_inputs;
	psmv->base.get_tracked_pose = psmv_device_get_tracked_pose;
	psmv->base.set_output = psmv_device_set_output;
	psmv->base.name = XRT_DEVICE_PSMV;
	psmv->base.binding_profiles = binding_profiles;
	psmv->base.num_binding_profiles = ARRAY_SIZE(binding_profiles);
	psmv->fusion.rot.w = 1.0f;
	psmv->fusion.fusion = imu_fusion_create();
	psmv->log_level = debug_get_log_option_psmv_log();
	psmv->pid = devices[index]->product_id;
	psmv->hid = hid;
	snprintf(psmv->base.str, XRT_DEVICE_NAME_LEN, "%s", "PS Move Controller");

	m_imu_pre_filter_init(&psmv->calibration.prefilter, 1.f, 1.f);

	// Default variance
	switch (devices[index]->product_id) {
	case PSMV_PID_ZCM1:
		// Note that there is one axis "weird" in each - this model has
		// 2-axis sensors.
		psmv->fusion.variance.accel.x = 0.00046343013089f;
		psmv->fusion.variance.accel.y = 0.000358375519793f;
		psmv->fusion.variance.accel.z = 0.000358375519793f;
		psmv->fusion.variance.gyro.x = 7.85920759635965E-05f;
		psmv->fusion.variance.gyro.y = 7.85920759635965E-05f;
		psmv->fusion.variance.gyro.z = 0.00051253981244f;
		break;
	case PSMV_PID_ZCM2:
		//! @todo measure!
		psmv->fusion.variance.accel.x = 0.00046343013089f;
		psmv->fusion.variance.accel.y = 0.000358375519793f;
		psmv->fusion.variance.accel.z = 0.000358375519793f;
		psmv->fusion.variance.gyro.x = 7.85920759635965E-05f;
		psmv->fusion.variance.gyro.y = 7.85920759635965E-05f;
		psmv->fusion.variance.gyro.z = 0.00051253981244f;
		break;
	default:
		//! @todo cleanup to not leak
		return -1;
	}

	// Setup inputs.
	SET_INPUT(PS_CLICK);
	SET_INPUT(MOVE_CLICK);
	SET_INPUT(START_CLICK);
	SET_INPUT(SELECT_CLICK);
	SET_INPUT(SQUARE_CLICK);
	SET_INPUT(CROSS_CLICK);
	SET_INPUT(CIRCLE_CLICK);
	SET_INPUT(TRIANGLE_CLICK);
	SET_INPUT(TRIGGER_VALUE);
	SET_INPUT(GRIP_POSE);
	SET_INPUT(AIM_POSE);
	SET_INPUT(BODY_CENTER_POSE);
	SET_INPUT(BALL_CENTER_POSE);

	// We only have one output.
	psmv->base.outputs[0].name = XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION;

	// Mutex before thread.
	ret = os_mutex_init(&psmv->lock);
	if (ret != 0) {
		PSMV_ERROR(psmv, "Failed to init mutex!");
		psmv_device_destroy(&psmv->base);
		return ret;
	}

	// Thread and other state.
	ret = os_thread_helper_init(&psmv->oth);
	if (ret != 0) {
		PSMV_ERROR(psmv, "Failed to init threading!");
		psmv_device_destroy(&psmv->base);
		return ret;
	}
	// Get calibration data.
	ret = psmv_get_calibration(psmv);
	if (ret != 0) {
		PSMV_ERROR(psmv, "Failed to get calibration data!");
		psmv_device_destroy(&psmv->base);
		return ret;
	}

#if 1
	// 45mm
	float diameter = PSMV_BALL_DIAMETER_M;
	(void)diameter;
	if (xp->tracking != NULL) {
		xp->tracking->create_tracked_psmv(xp->tracking, &psmv->base, &psmv->ball);
	}
#endif

	if (psmv->ball != NULL) {
		// Use the new origin if we got a tracking system.
		psmv->base.tracking_origin = psmv->ball->origin;

		// We got a tracked ball, use it.
		psmv->base.tracking_origin = psmv->ball->origin;
		psmv->wants.led.r = psmv_clamp_zero_to_one_float_to_u8(psmv->ball->colour.r);
		psmv->wants.led.g = psmv_clamp_zero_to_one_float_to_u8(psmv->ball->colour.g);
		psmv->wants.led.b = psmv_clamp_zero_to_one_float_to_u8(psmv->ball->colour.b);

	} else {
		// Failed to create a tracking ball.
		static int hack = 0;
		switch (hack++ % 3) {
		case 0: psmv->wants.led.r = 0xff; break;
		case 1:
			psmv->wants.led.r = 0xff;
			psmv->wants.led.b = 0xff;
			break;
		case 2: psmv->wants.led.b = 0xff; break;
		}
	}

	// Send the first update package.
	psmv_led_and_trigger_update(psmv, 1);

	ret = os_thread_helper_start(&psmv->oth, psmv_run_thread, psmv);
	if (ret != 0) {
		PSMV_ERROR(psmv, "Failed to start thread!");
		psmv_device_destroy(&psmv->base);
		return ret;
	}

	// Start the variable tracking now that everything is in place.
	// clang-format off
	u_var_add_root(psmv, "PSMV Controller", true);
	u_var_add_gui_header(psmv, &psmv->gui.calibration, "Calibration");
	switch (psmv->pid) {
	case PSMV_PID_ZCM1:
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm1.accel_min_x, "zcm1.accel_min_x");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm1.accel_max_x, "zcm1.accel_max_x");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm1.accel_min_y, "zcm1.accel_min_y");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm1.accel_max_y, "zcm1.accel_max_y");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm1.accel_min_z, "zcm1.accel_min_z");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm1.accel_max_z, "zcm1.accel_max_z");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm1.gyro_rot_x, "zcm1.gyro_rot_x");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm1.gyro_rot_y, "zcm1.gyro_rot_y");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm1.gyro_rot_z, "zcm1.gyro_rot_z");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm1.gyro_bias_0, "zcm1.gyro_bias_0");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm1.gyro_bias_1, "zcm1.gyro_bias_1");
		u_var_add_vec3_f32(psmv, &psmv->calibration.zcm1.gyro_fact, "zcm1.gyro_fact");
		break;
	case PSMV_PID_ZCM2:
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.accel_min_x, "zcm2.accel_min_x");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.accel_max_x, "zcm2.accel_max_x");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.accel_min_y, "zcm2.accel_min_y");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.accel_max_y, "zcm2.accel_max_y");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.accel_min_z, "zcm2.accel_min_z");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.accel_max_z, "zcm2.accel_max_z");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.gyro_neg_x, "zcm2.gyro_neg_x");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.gyro_pos_x, "zcm2.gyro_pos_x");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.gyro_neg_y, "zcm2.gyro_neg_y");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.gyro_pos_y, "zcm2.gyro_pos_y");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.gyro_neg_z, "zcm2.gyro_neg_z");
		u_var_add_vec3_i32(psmv, &psmv->calibration.zcm2.gyro_pos_z, "zcm2.gyro_pos_z");
		break;
	default: assert(false);
	}
	u_var_add_vec3_f32(psmv, &psmv->calibration.prefilter.accel.gain, "prefilter.accel.gain");
	u_var_add_vec3_f32(psmv, &psmv->calibration.prefilter.accel.bias, "prefilter.accel.bias");
	u_var_add_vec3_f32(psmv, &psmv->calibration.prefilter.gyro.gain, "prefilter.gyro.gain");
	u_var_add_vec3_f32(psmv, &psmv->calibration.prefilter.gyro.bias, "prefilter.gyro.bias");
	u_var_add_gui_header(psmv, &psmv->gui.last_frame, "Last data");
	u_var_add_ro_vec3_i32(psmv, &psmv->last.samples[0].accel, "last.samples[0].accel");
	u_var_add_ro_vec3_i32(psmv, &psmv->last.samples[1].accel, "last.samples[1].accel");
	u_var_add_ro_vec3_i32(psmv, &psmv->last.samples[0].gyro, "last.samples[0].gyro");
	u_var_add_ro_vec3_i32(psmv, &psmv->last.samples[1].gyro, "last.samples[1].gyro");
	u_var_add_ro_vec3_f32(psmv, &psmv->read.accel, "read.accel");
	u_var_add_ro_vec3_f32(psmv, &psmv->read.gyro, "read.gyro");
	u_var_add_gui_header(psmv, &psmv->gui.fusion, "Fusion");
	u_var_add_vec3_f32(psmv, &psmv->fusion.variance.accel, "fusion.variance.accel");
	u_var_add_vec3_f32(psmv, &psmv->fusion.variance.gyro, "fusion.variance.gyro");
	u_var_add_ro_quat_f32(psmv, &psmv->fusion.rot, "fusion.rot");
	u_var_add_ro_vec3_f32(psmv, &psmv->fusion.rotvec, "fusion.rotvec");
	u_var_add_gui_header(psmv, &psmv->gui.control, "Control");
	u_var_add_rgb_u8(psmv, &psmv->wants.led, "Led");
	u_var_add_u8(psmv, &psmv->wants.rumble, "Rumble");
	u_var_add_log_level(psmv, &psmv->log_level, "Log level");
	// clang-format on

	psmv->base.orientation_tracking_supported = true;
	psmv->base.position_tracking_supported = psmv->ball != NULL;
	psmv->base.device_type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER;

	// And finally done
	*out_xdevs = &psmv->base;

	return 1;
}


/*
 *
 * Parsing functions
 *
 */

static void
psmv_i32_from_u16_wire(int32_t *to, const struct psmv_u16_wire *from)
{
	*to = (from->low | from->high << 8) - 0x8000;
}

static void
psmv_i32_from_i16_wire(int32_t *to, const struct psmv_i16_wire *from)
{
	// The cast is important, sign extend properly.
	*to = (int16_t)(from->low | from->high << 8);
}

static void
psmv_from_vec3_u16_wire(struct xrt_vec3_i32 *to, const struct psmv_vec3_u16_wire *from)
{
	psmv_i32_from_u16_wire(&to->x, &from->x);
	psmv_i32_from_u16_wire(&to->y, &from->y);
	psmv_i32_from_u16_wire(&to->z, &from->z);
}

static void
psmv_from_vec3_i16_wire(struct xrt_vec3_i32 *to, const struct psmv_vec3_i16_wire *from)
{
	psmv_i32_from_i16_wire(&to->x, &from->x);
	psmv_i32_from_i16_wire(&to->y, &from->y);
	psmv_i32_from_i16_wire(&to->z, &from->z);
}

static void
psmv_f32_from_wire(float *to, const struct psmv_f32_wire *from)
{
	union {
		uint32_t wire;
		float f32;
	} safe_copy;

	safe_copy.wire = (from->val[0] << 0) | (from->val[1] << 8) | (from->val[2] << 16) | (from->val[3] << 24);
	*to = safe_copy.f32;
}

static void
psmv_from_vec3_f32_wire(struct xrt_vec3 *to, const struct psmv_vec3_f32_wire *from)
{
	psmv_f32_from_wire(&to->x, &from->x);
	psmv_f32_from_wire(&to->y, &from->y);
	psmv_f32_from_wire(&to->z, &from->z);
}


/*
 *
 * Packet functions ZCM1
 *
 */

static int
psmv_get_calibration_zcm1(struct psmv_device *psmv)
{
	struct psmv_parsed_calibration_zcm1 *zcm1 = &psmv->calibration.zcm1;
	struct psmv_calibration_zcm1 data;
	uint8_t *dst = (uint8_t *)&data;
	int ret = 0;
	size_t src_offset, dst_offset;

	for (int i = 0; i < 3; i++) {
		struct psmv_calibration_part part = {0};
		uint8_t *src = (uint8_t *)&part;

		part.id = 0x10;

		ret = os_hid_get_feature(psmv->hid, 0x10, src, sizeof(part));
		if (ret < 0) {
			PSMV_ERROR(psmv, "os_hid_get_feature returned %i", ret);
			return ret;
		}

		if (ret != (int)sizeof(part)) {
			PSMV_ERROR(psmv, "Size wrong: %i != %i", ret, (int)sizeof(part));
			return -1;
		}

		switch (part.which) {
		case 0x00:
			src_offset = 0;
			dst_offset = 0;
			break;
		case 0x01:
			src_offset = 2;
			dst_offset = sizeof(part);
			break;
		case 0x82:
			src_offset = 2;
			dst_offset = sizeof(part) * 2 - 2;
			break;
		default: PSMV_ERROR(psmv, "Unexpected part id! %i", part.which); return -1;
		}

		memcpy(dst + dst_offset, src + src_offset, sizeof(part) - src_offset);
	}

	psmv_from_vec3_u16_wire(&zcm1->accel_min_x, &data.accel_min_x);
	psmv_from_vec3_u16_wire(&zcm1->accel_max_x, &data.accel_max_x);
	psmv_from_vec3_u16_wire(&zcm1->accel_min_y, &data.accel_min_y);
	psmv_from_vec3_u16_wire(&zcm1->accel_max_y, &data.accel_max_y);
	psmv_from_vec3_u16_wire(&zcm1->accel_min_z, &data.accel_min_z);
	psmv_from_vec3_u16_wire(&zcm1->accel_max_z, &data.accel_max_z);
	psmv_from_vec3_u16_wire(&zcm1->gyro_bias_0, &data.gyro_bias_0);
	psmv_from_vec3_u16_wire(&zcm1->gyro_bias_1, &data.gyro_bias_1);
	psmv_from_vec3_u16_wire(&zcm1->gyro_rot_x, &data.gyro_rot_x);
	psmv_from_vec3_u16_wire(&zcm1->gyro_rot_y, &data.gyro_rot_y);
	psmv_from_vec3_u16_wire(&zcm1->gyro_rot_z, &data.gyro_rot_z);
	psmv_from_vec3_f32_wire(&zcm1->gyro_fact, &data.gyro_fact);
	psmv_from_vec3_f32_wire(&zcm1->unknown_vec3, &data.unknown_vec3);
	psmv_f32_from_wire(&zcm1->unknown_float_0, &data.unknown_float_0);
	psmv_f32_from_wire(&zcm1->unknown_float_1, &data.unknown_float_1);

	/*
	 * See the following reference for details on the computations below. We
	 * are currently pretending cross-gains are zero.
	 *
	 * Vitali, Andrea. “6-Point Tumble Sensor Calibration.” Design tip.
	 * STMicroelectronics, 2015.
	 * https://www.st.com/resource/en/design_tip/dm00253745-6point-tumble-sensor-calibration-stmicroelectronics.pdf.
	 */

	/*
	 * Acceleration
	 */

	psmv->calibration.prefilter.accel.gain.x =
	    MATH_GRAVITY_M_S2 / ((zcm1->accel_max_x.x - zcm1->accel_min_x.x) / 2.0);
	psmv->calibration.prefilter.accel.gain.y =
	    MATH_GRAVITY_M_S2 / ((zcm1->accel_max_y.y - zcm1->accel_min_y.y) / 2.0);
	psmv->calibration.prefilter.accel.gain.z =
	    MATH_GRAVITY_M_S2 / ((zcm1->accel_max_z.z - zcm1->accel_min_z.z) / 2.0);

	psmv->calibration.prefilter.accel.bias.x = (zcm1->accel_min_x.x + zcm1->accel_max_x.x + zcm1->accel_min_y.x +
	                                            zcm1->accel_max_y.x + zcm1->accel_min_z.x + zcm1->accel_max_z.x) /
	                                           6.0;
	psmv->calibration.prefilter.accel.bias.y = (zcm1->accel_min_x.y + zcm1->accel_max_x.y + zcm1->accel_min_y.y +
	                                            zcm1->accel_max_y.y + zcm1->accel_min_z.y + zcm1->accel_max_z.y) /
	                                           6.0;
	psmv->calibration.prefilter.accel.bias.z = (zcm1->accel_min_x.z + zcm1->accel_max_x.z + zcm1->accel_min_y.z +
	                                            zcm1->accel_max_y.z + zcm1->accel_min_z.z + zcm1->accel_max_z.z) /
	                                           6.0;

	/*
	 * Gyro
	 */

	double gx = (zcm1->gyro_rot_x.x - (zcm1->gyro_bias_0.x * zcm1->gyro_fact.x));
	double gy = (zcm1->gyro_rot_y.y - (zcm1->gyro_bias_0.y * zcm1->gyro_fact.y));
	double gz = (zcm1->gyro_rot_z.z - (zcm1->gyro_bias_0.z * zcm1->gyro_fact.z));

	psmv->calibration.prefilter.gyro.gain.x = (2.0 * M_PI * 80.0) / (60.0 * gx);
	psmv->calibration.prefilter.gyro.gain.y = (2.0 * M_PI * 80.0) / (60.0 * gy);
	psmv->calibration.prefilter.gyro.gain.z = (2.0 * M_PI * 80.0) / (60.0 * gz);
	psmv->calibration.prefilter.gyro.bias.x = 0.0;
	psmv->calibration.prefilter.gyro.bias.y = 0.0;
	psmv->calibration.prefilter.gyro.bias.z = 0.0;


	/*
	 * Print
	 */

	PSMV_DEBUG(
	    psmv,
	    "\n"
	    "\tCalibration:\n"
	    "\t\taccel_min_x: %6i %6i %6i\n"
	    "\t\taccel_max_x: %6i %6i %6i\n"
	    "\t\taccel_min_y: %6i %6i %6i\n"
	    "\t\taccel_max_y: %6i %6i %6i\n"
	    "\t\taccel_min_z: %6i %6i %6i\n"
	    "\t\taccel_max_z: %6i %6i %6i\n"
	    "\t\tgyro_rot_x:  %6i %6i %6i\n"
	    "\t\tgyro_rot_y:  %6i %6i %6i\n"
	    "\t\tgyro_rot_z:  %6i %6i %6i\n"
	    "\t\tgyro_bias_0: %6i %6i %6i\n"
	    "\t\tgyro_bias_1: %6i %6i %6i\n"
	    "\t\tgyro_fact: %f %f %f\n"
	    "\t\tunknown_vec3: %f %f %f\n"
	    "\t\tunknown_float_0 %f\n"
	    "\t\tunknown_float_1 %f\n"
	    "\tCalculated:\n"
	    "\t\taccel.gain: %f %f %f\n"
	    "\t\taccel.bias: %f %f %f\n"
	    "\t\tgyro.gain: %f %f %f\n"
	    "\t\tgyro.bias: %f %f %f\n",
	    zcm1->accel_min_x.x, zcm1->accel_min_x.y, zcm1->accel_min_x.z, zcm1->accel_max_x.x, zcm1->accel_max_x.y,
	    zcm1->accel_max_x.z, zcm1->accel_min_y.x, zcm1->accel_min_y.y, zcm1->accel_min_y.z, zcm1->accel_max_y.x,
	    zcm1->accel_max_y.y, zcm1->accel_max_y.z, zcm1->accel_min_z.x, zcm1->accel_min_z.y, zcm1->accel_min_z.z,
	    zcm1->accel_max_z.x, zcm1->accel_max_z.y, zcm1->accel_max_z.z, zcm1->gyro_rot_x.x, zcm1->gyro_rot_x.y,
	    zcm1->gyro_rot_x.z, zcm1->gyro_rot_y.x, zcm1->gyro_rot_y.y, zcm1->gyro_rot_y.z, zcm1->gyro_rot_z.x,
	    zcm1->gyro_rot_z.y, zcm1->gyro_rot_z.z, zcm1->gyro_bias_0.x, zcm1->gyro_bias_0.y, zcm1->gyro_bias_0.z,
	    zcm1->gyro_bias_1.x, zcm1->gyro_bias_1.y, zcm1->gyro_bias_1.z, zcm1->gyro_fact.x, zcm1->gyro_fact.y,
	    zcm1->gyro_fact.z, zcm1->unknown_vec3.x, zcm1->unknown_vec3.y, zcm1->unknown_vec3.z, zcm1->unknown_float_0,
	    zcm1->unknown_float_1, psmv->calibration.prefilter.accel.gain.x, psmv->calibration.prefilter.accel.gain.y,
	    psmv->calibration.prefilter.accel.gain.z, psmv->calibration.prefilter.accel.bias.x,
	    psmv->calibration.prefilter.accel.bias.y, psmv->calibration.prefilter.accel.bias.z,
	    psmv->calibration.prefilter.gyro.gain.x, psmv->calibration.prefilter.gyro.gain.y,
	    psmv->calibration.prefilter.gyro.gain.z, psmv->calibration.prefilter.gyro.bias.x,
	    psmv->calibration.prefilter.gyro.bias.y, psmv->calibration.prefilter.gyro.bias.z);

	return 0;
}

static int
psmv_parse_input_zcm1(struct psmv_device *psmv, struct psmv_input_zcm1 *data, struct psmv_parsed_input *input)
{
	input->battery = data->battery;
	input->seq_no = data->buttons[3] & 0x0f;

	input->buttons = 0;
	input->buttons |= data->buttons[0] << 24;
	input->buttons |= data->buttons[1] << 16;
	input->buttons |= data->buttons[2] << 8;
	input->buttons |= data->buttons[3] & 0xf0;
	input->timestamp = 0;
	input->timestamp |= (uint16_t)data->timestamp_low;
	input->timestamp |= ((uint16_t)data->timestamp_high) << 8;

	input->trigger_values[0] = data->trigger_f1;
	input->trigger_values[1] = data->trigger_f2;

	psmv_from_vec3_u16_wire(&input->samples[0].accel, &data->accel_f1);
	psmv_from_vec3_u16_wire(&input->samples[0].gyro, &data->gyro_f1);

	psmv_from_vec3_u16_wire(&input->samples[1].accel, &data->accel_f2);
	psmv_from_vec3_u16_wire(&input->samples[1].gyro, &data->gyro_f2);

	uint32_t diff = psmv_calc_delta_and_handle_rollover(input->timestamp, psmv->last.timestamp);
	bool missed = input->seq_no != ((psmv->last.seq_no + 1) & 0x0f);


	/*
	 * Print
	 */

	PSMV_TRACE(psmv,
	           "\n\t"
	           "missed: %s\n\t"
	           "buttons: %08x\n\t"
	           "battery: %x\n\t"
	           "samples[0].accel: %6i %6i %6i\n\t"
	           "samples[1].accel: %6i %6i %6i\n\t"
	           "samples[0].gyro:  %6i %6i %6i\n\t"
	           "samples[1].gyro:  %6i %6i %6i\n\t"
	           "trigger_values[0]: %02x\n\t"
	           "trigger_values[1]: %02x\n\t"
	           "timestamp: %i\n\t"
	           "diff: %i\n\t"
	           "seq_no: %x\n",
	           missed ? "yes" : "no", input->buttons, input->battery, input->samples[0].accel.x,
	           input->samples[0].accel.y, input->samples[0].accel.z, input->samples[1].accel.x,
	           input->samples[1].accel.y, input->samples[1].accel.z, input->samples[0].gyro.x,
	           input->samples[0].gyro.y, input->samples[0].gyro.z, input->samples[1].gyro.x,
	           input->samples[1].gyro.y, input->samples[1].gyro.z, input->trigger_values[0],
	           input->trigger_values[1], input->timestamp, diff, input->seq_no);

	return 2;
}


/*
 *
 * Packet functions ZCM2
 *
 */

static int
psmv_get_calibration_zcm2(struct psmv_device *psmv)
{
	struct psmv_parsed_calibration_zcm2 *zcm2 = &psmv->calibration.zcm2;
	struct psmv_calibration_zcm2 data;
	uint8_t *dst = (uint8_t *)&data;
	int ret = 0;
	size_t src_offset, dst_offset;

	for (int i = 0; i < 2; i++) {
		struct psmv_calibration_part part = {0};
		uint8_t *src = (uint8_t *)&part;

		part.id = 0x10;

		ret = os_hid_get_feature(psmv->hid, 0x10, src, sizeof(part));
		if (ret < 0) {
			PSMV_ERROR(psmv, "os_hid_get_feature returned %i", ret);
			return ret;
		}

		if (ret != (int)sizeof(part)) {
			PSMV_ERROR(psmv, "Size wrong: %i != %i", ret, (int)sizeof(part));
			return -1;
		}

		switch (part.which) {
		case 0x00:
			src_offset = 0;
			dst_offset = 0;
			break;
		case 0x81:
			src_offset = 2;
			dst_offset = sizeof(part);
			break;
		default: PSMV_ERROR(psmv, "Unexpected part id! %i", part.which); return -1;
		}

		memcpy(dst + dst_offset, src + src_offset, sizeof(part) - src_offset);
	}

	psmv_from_vec3_i16_wire(&zcm2->accel_min_x, &data.accel_min_x);
	psmv_from_vec3_i16_wire(&zcm2->accel_max_x, &data.accel_max_x);
	psmv_from_vec3_i16_wire(&zcm2->accel_min_y, &data.accel_min_y);
	psmv_from_vec3_i16_wire(&zcm2->accel_max_y, &data.accel_max_y);
	psmv_from_vec3_i16_wire(&zcm2->accel_min_z, &data.accel_min_z);
	psmv_from_vec3_i16_wire(&zcm2->accel_max_z, &data.accel_max_z);

	psmv_from_vec3_i16_wire(&zcm2->gyro_neg_x, &data.gyro_neg_x);
	psmv_from_vec3_i16_wire(&zcm2->gyro_pos_x, &data.gyro_pos_x);
	psmv_from_vec3_i16_wire(&zcm2->gyro_neg_y, &data.gyro_neg_y);
	psmv_from_vec3_i16_wire(&zcm2->gyro_pos_y, &data.gyro_pos_y);
	psmv_from_vec3_i16_wire(&zcm2->gyro_neg_z, &data.gyro_neg_z);
	psmv_from_vec3_i16_wire(&zcm2->gyro_pos_z, &data.gyro_pos_z);
	psmv_from_vec3_i16_wire(&zcm2->gyro_bias, &data.gyro_bias);


	/*
	 * Acceleration
	 */

	psmv->calibration.prefilter.accel.gain.x =
	    MATH_GRAVITY_M_S2 / ((zcm2->accel_max_x.x - zcm2->accel_min_x.x) / 2.0);
	psmv->calibration.prefilter.accel.gain.y =
	    MATH_GRAVITY_M_S2 / ((zcm2->accel_max_y.y - zcm2->accel_min_y.y) / 2.0);
	psmv->calibration.prefilter.accel.gain.z =
	    MATH_GRAVITY_M_S2 / ((zcm2->accel_max_z.z - zcm2->accel_min_z.z) / 2.0);

	psmv->calibration.prefilter.accel.bias.x = (zcm2->accel_min_x.x + zcm2->accel_max_x.x + zcm2->accel_min_y.x +
	                                            zcm2->accel_max_y.x + zcm2->accel_min_z.x + zcm2->accel_max_z.x) /
	                                           6.0;
	psmv->calibration.prefilter.accel.bias.y = (zcm2->accel_min_x.y + zcm2->accel_max_x.y + zcm2->accel_min_y.y +
	                                            zcm2->accel_max_y.y + zcm2->accel_min_z.y + zcm2->accel_max_z.y) /
	                                           6.0;
	psmv->calibration.prefilter.accel.bias.z = (zcm2->accel_min_x.z + zcm2->accel_max_x.z + zcm2->accel_min_y.z +
	                                            zcm2->accel_max_y.z + zcm2->accel_min_z.z + zcm2->accel_max_z.z) /
	                                           6.0;


	/*
	 * Gyro
	 */

	double gx = (zcm2->gyro_pos_x.x - zcm2->gyro_neg_x.x) / 2.0;
	double gy = (zcm2->gyro_pos_y.y - zcm2->gyro_neg_y.y) / 2.0;
	double gz = (zcm2->gyro_pos_z.z - zcm2->gyro_neg_z.z) / 2.0;

	psmv->calibration.prefilter.gyro.gain.x = (2.0 * M_PI * 90.0) / (60.0 * gx);
	psmv->calibration.prefilter.gyro.gain.y = (2.0 * M_PI * 90.0) / (60.0 * gy);
	psmv->calibration.prefilter.gyro.gain.z = (2.0 * M_PI * 90.0) / (60.0 * gz);

#if 0
	psmv->calibration.prefilter.gyro.bias.x =
	    (zcm2->gyro_neg_y.x + zcm2->gyro_pos_y.x + zcm2->gyro_neg_z.x +
	     zcm2->gyro_pos_z.x) /
	    4.0;
	psmv->calibration.prefilter.gyro.bias.y =
	    (zcm2->gyro_neg_x.y + zcm2->gyro_pos_x.y + zcm2->gyro_neg_z.y +
	     zcm2->gyro_pos_z.y) /
	    4.0;
	psmv->calibration.prefilter.gyro.bias.z =
	    (zcm2->gyro_neg_x.z + zcm2->gyro_pos_x.z + zcm2->gyro_neg_y.z +
	     zcm2->gyro_pos_y.z) /
	    4.0;
#else
	psmv->calibration.prefilter.gyro.bias.x = zcm2->gyro_bias.x;
	psmv->calibration.prefilter.gyro.bias.y = zcm2->gyro_bias.y;
	psmv->calibration.prefilter.gyro.bias.z = zcm2->gyro_bias.z;
#endif


	/*
	 * Print
	 */

	PSMV_DEBUG(psmv,
	           "\n"
	           "\tCalibration:\n"
	           "\t\taccel_min_x: %6i %6i %6i\n"
	           "\t\taccel_max_x: %6i %6i %6i\n"
	           "\t\taccel_min_y: %6i %6i %6i\n"
	           "\t\taccel_max_y: %6i %6i %6i\n"
	           "\t\taccel_min_z: %6i %6i %6i\n"
	           "\t\taccel_max_z: %6i %6i %6i\n"
	           "\t\tgyro_neg_x:  %6i %6i %6i\n"
	           "\t\tgyro_pos_x:  %6i %6i %6i\n"
	           "\t\tgyro_neg_y:  %6i %6i %6i\n"
	           "\t\tgyro_pos_y:  %6i %6i %6i\n"
	           "\t\tgyro_neg_z:  %6i %6i %6i\n"
	           "\t\tgyro_pos_z:  %6i %6i %6i\n"
	           "\t\tgyro_bias:  %6i %6i %6i\n"
	           "\tCalculated:\n"
	           "\t\taccel.gain: %f %f %f\n"
	           "\t\taccel.bias: %f %f %f\n"
	           "\t\tgyro.gain: %f %f %f\n"
	           "\t\tgyro.bias: %f %f %f\n",
	           zcm2->accel_min_x.x, zcm2->accel_min_x.y, zcm2->accel_min_x.z, zcm2->accel_max_x.x,
	           zcm2->accel_max_x.y, zcm2->accel_max_x.z, zcm2->accel_min_y.x, zcm2->accel_min_y.y,
	           zcm2->accel_min_y.z, zcm2->accel_max_y.x, zcm2->accel_max_y.y, zcm2->accel_max_y.z,
	           zcm2->accel_min_z.x, zcm2->accel_min_z.y, zcm2->accel_min_z.z, zcm2->accel_max_z.x,
	           zcm2->accel_max_z.y, zcm2->accel_max_z.z, zcm2->gyro_neg_x.x, zcm2->gyro_neg_x.y, zcm2->gyro_neg_x.z,
	           zcm2->gyro_pos_x.x, zcm2->gyro_pos_x.y, zcm2->gyro_pos_x.z, zcm2->gyro_neg_y.x, zcm2->gyro_neg_y.y,
	           zcm2->gyro_neg_y.z, zcm2->gyro_pos_y.x, zcm2->gyro_pos_y.y, zcm2->gyro_pos_y.z, zcm2->gyro_neg_z.x,
	           zcm2->gyro_neg_z.y, zcm2->gyro_neg_z.z, zcm2->gyro_pos_z.x, zcm2->gyro_pos_z.y, zcm2->gyro_pos_z.z,
	           zcm2->gyro_bias.x, zcm2->gyro_bias.y, zcm2->gyro_bias.z, psmv->calibration.prefilter.accel.gain.x,
	           psmv->calibration.prefilter.accel.gain.y, psmv->calibration.prefilter.accel.gain.z,
	           psmv->calibration.prefilter.accel.bias.x, psmv->calibration.prefilter.accel.bias.y,
	           psmv->calibration.prefilter.accel.bias.z, psmv->calibration.prefilter.gyro.gain.x,
	           psmv->calibration.prefilter.gyro.gain.y, psmv->calibration.prefilter.gyro.gain.z,
	           psmv->calibration.prefilter.gyro.bias.x, psmv->calibration.prefilter.gyro.bias.y,
	           psmv->calibration.prefilter.gyro.bias.z);

	return 0;
}

static int
psmv_parse_input_zcm2(struct psmv_device *psmv, struct psmv_input_zcm2 *data, struct psmv_parsed_input *input)
{
	input->battery = data->battery;
	input->seq_no = data->buttons[3] & 0x0f;

	input->buttons = 0;
	input->buttons |= data->buttons[0] << 24;
	input->buttons |= data->buttons[1] << 16;
	input->buttons |= data->buttons[2] << 8;
	input->buttons |= data->buttons[3] & 0xf0;
	input->timestamp = 0;
	input->timestamp |= (uint16_t)data->timestamp_low;
	input->timestamp |= ((uint16_t)data->timestamp_high) << 8;
	input->timestamp_copy = 0;
	input->timestamp_copy |= (uint16_t)data->timestamp_low_copy;
	input->timestamp_copy |= ((uint16_t)data->timestamp_high_copy) << 8;
	input->trigger_low_pass = data->trigger_low_pass;
	input->trigger = data->trigger;

	psmv_from_vec3_i16_wire(&input->sample.accel, &data->accel);
	psmv_from_vec3_i16_wire(&input->sample.gyro, &data->gyro);

	psmv_from_vec3_i16_wire(&input->sample_copy.accel, &data->accel_copy);
	psmv_from_vec3_i16_wire(&input->sample_copy.gyro, &data->gyro_copy);

	uint32_t diff = psmv_calc_delta_and_handle_rollover(input->timestamp, psmv->last.timestamp);
	bool missed = input->seq_no != ((psmv->last.seq_no + 1) & 0x0f);


	/*
	 * Print
	 */

	PSMV_TRACE(psmv,
	           "\n\t"
	           "missed: %s\n\t"
	           "buttons: %08x\n\t"
	           "battery: %x\n\t"
	           "sample.accel:      %6i %6i %6i\n\t"
	           "sample_copy.accel: %6i %6i %6i\n\t"
	           "sample.gyro:       %6i %6i %6i\n\t"
	           "sample_copy.gyro:  %6i %6i %6i\n\t"
	           "sample.trigger: %02x\n\t"
	           "sample.trigger_low_pass: %02x\n\t"
	           "timestamp:      %04x\n\t"
	           "timestamp_copy: %04x\n\t"
	           "diff: %i\n\t"
	           "seq_no: %x\n",
	           missed ? "yes" : "no", input->buttons, input->battery, input->samples[0].accel.x,
	           input->samples[0].accel.y, input->samples[0].accel.z, input->samples[1].accel.x,
	           input->samples[1].accel.y, input->samples[1].accel.z, input->samples[0].gyro.x,
	           input->samples[0].gyro.y, input->samples[0].gyro.z, input->samples[1].gyro.x,
	           input->samples[1].gyro.y, input->samples[1].gyro.z, input->trigger_low_pass, input->trigger,
	           input->timestamp, input->timestamp_copy, diff, input->seq_no);

	return 1;
}


/*
 *
 * Small dispatch functions.
 *
 */

static int
psmv_get_calibration(struct psmv_device *psmv)
{
	switch (psmv->pid) {
	case PSMV_PID_ZCM1: return psmv_get_calibration_zcm1(psmv);
	case PSMV_PID_ZCM2: return psmv_get_calibration_zcm2(psmv);
	default: return -1;
	}

	return 0;
}

static int
psmv_parse_input(struct psmv_device *psmv, void *data, struct psmv_parsed_input *input)
{
	U_ZERO(input);

	switch (psmv->pid) {
	case PSMV_PID_ZCM1: return psmv_parse_input_zcm1(psmv, (struct psmv_input_zcm1 *)data, input);
	case PSMV_PID_ZCM2: return psmv_parse_input_zcm2(psmv, (struct psmv_input_zcm2 *)data, input);
	default: return 0;
	}
}


/*!
 * @}
 */
