// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PSVR prober code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_psmv
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

#include "xrt/xrt_prober.h"

#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"

#include "psmv_interface.h"
#include <unistd.h>


/*
 *
 * Defines & structs.
 *
 */

#define PSMV_SPEW(p, ...)                                                      \
	do {                                                                   \
		if (p->print_spew) {                                           \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define PSMV_DEBUG(p, ...)                                                     \
	do {                                                                   \
		if (p->print_debug) {                                          \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define PSMV_ERROR(p, ...)                                                     \
	do {                                                                   \
		fprintf(stderr, "%s - ", __func__);                            \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, "\n");                                         \
	} while (false)

DEBUG_GET_ONCE_BOOL_OPTION(psmv_spew, "PSMV_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(psmv_debug, "PSMV_PRINT_DEBUG", false)

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
	PSMV_INDEX_BODY_CENTER_POSE,
	PSMV_INDEX_BALL_CENTER_POSE,
	PSMV_INDEX_BALL_TIP_POSE,
};

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
	PSMV_BUTTON_BIT_SQUARE      = (1 << 22),
	PSMV_BUTTON_BIT_CROSS       = (1 << 23),

	PSMV_BUTTON_BIT_START       = (1 << 27),
	PSMV_BUTTON_BIT_SELECT      = (1 << 24),

	PSMV_BUTTON_BIT_MOVE_ANY    = PSMV_BUTTON_BIT_MOVE_F1 |
	                              PSMV_BUTTON_BIT_MOVE_F2,
	PSMV_BUTTON_BIT_TRIGGER_ANY = PSMV_BUTTON_BIT_TRIGGER_F1 |
	                              PSMV_BUTTON_BIT_TRIGGER_F2,
	// clang-format on
};



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
 * Wire encoding of a single 32 bit float, big endian.
 *
 * @ingroup drv_psmv
 */
struct psmv_f32_wire
{
	uint8_t val[4];
};

/*!
 * Wire encoding of three 32 bit float, notice order of axis, big endian.
 *
 * @ingroup drv_psmv
 */
struct psmv_vec3_f32_wite
{
	struct psmv_f32_wire x;
	struct psmv_f32_wire z;
	struct psmv_f32_wire y;
};

/*!
 * Wire encoding of a single 16 bit integer, big endian.
 *
 * @ingroup drv_psmv
 */
struct psmv_i16_wire
{
	uint8_t low;
	uint8_t high;
};

/*!
 * Wire encoding of three 16 bit integers, notice order of axis.
 *
 * @ingroup drv_psmv
 */
struct psmv_vec3_i16_wire
{
	struct psmv_i16_wire x;
	struct psmv_i16_wire z;
	struct psmv_i16_wire y;
};

/*!
 * A normal i32 3 element vector.
 *
 * @ingroup drv_psmv
 */
struct psmv_vec3_i32
{
	int32_t x;
	int32_t y;
	int32_t z;
};

/*!
 * Input package.
 *
 * @ingroup drv_psmv
 */
struct psmv_get_input
{
	uint8_t header;
	uint8_t buttons[4];
	uint8_t trigger_f1;
	uint8_t trigger_f2;
	uint8_t unknown[4];
	uint8_t timestamp_high;
	uint8_t battery;
	struct psmv_vec3_i16_wire accel_f1;
	struct psmv_vec3_i16_wire accel_f2;
	struct psmv_vec3_i16_wire gyro_f1;
	struct psmv_vec3_i16_wire gyro_f2;
	uint8_t temp_mag[6];
	uint8_t timestamp_low;
	uint8_t pad[49 - 44];
};

/*!
 * Part of a calibration data, multiple packets make up a single data packet.
 *
 * @ingroup drv_psmv
 */
struct psmv_calibration_part
{
	uint8_t id;
	uint8_t which;
	uint8_t data[49 - 2];
};

/*!
 * Calibration data, multiple packets goes into this.
 *
 * @ingroup drv_psmv
 */
struct psmv_calibration_zcm1
{
	uint8_t id;
	uint8_t which;
	uint16_t _pad0;
	struct psmv_vec3_i16_wire accel_max_y;
	struct psmv_vec3_i16_wire accel_min_x;
	struct psmv_vec3_i16_wire accel_min_y;
	struct psmv_vec3_i16_wire accel_max_x;
	struct psmv_vec3_i16_wire accel_max_z;
	struct psmv_vec3_i16_wire accel_min_z;
	uint16_t _pad1;
	struct psmv_vec3_i16_wire gyro_bias_0;
	uint16_t _pad2;
	struct psmv_vec3_i16_wire gyro_bias_1;
	uint8_t _pad3[7];
	uint8_t _pad4;
	uint16_t _pad5;
	uint16_t _pad6;
	uint16_t _pad7;
	struct psmv_vec3_i16_wire gyro_rot_x;
	uint16_t _pad8;
	struct psmv_vec3_i16_wire gyro_rot_z;
	uint16_t _pad9;
	struct psmv_vec3_i16_wire gyro_rot_y;
	uint16_t _pad10;
	struct psmv_vec3_f32_wite unknown_vec3;
	struct psmv_vec3_f32_wite gyro_fact;
	struct psmv_f32_wire unknown_float_0;
	struct psmv_f32_wire unknown_float_1;
	uint8_t _pad[17];
};

struct psmv_parsed_input
{
	uint32_t buttons;
	uint16_t timestamp;
	uint8_t battery;
	uint8_t seq_no;

	struct
	{
		struct psmv_vec3_i32 accel;
		struct psmv_vec3_i32 gyro;
		uint8_t trigger;
	} frame[2];
};

/*!
 * A single PlayStation Move Controller.
 *
 * @ingroup drv_psmv
 */
struct psmv_device
{
	struct xrt_device base;

	struct os_hid_device *hid;

	int64_t resend_time;
	struct
	{
		uint8_t red;
		uint8_t green;
		uint8_t blue;
		uint8_t rumble;
	} state;

	//! Last sensor read timestamp.
	struct
	{
		uint32_t buttons;
		uint8_t trigger;
		uint16_t timestamp;
		uint8_t seqno;
	} last;

	struct psmv_vec3_i32 accel_min_x;
	struct psmv_vec3_i32 accel_max_x;
	struct psmv_vec3_i32 accel_min_y;
	struct psmv_vec3_i32 accel_max_y;
	struct psmv_vec3_i32 accel_min_z;
	struct psmv_vec3_i32 accel_max_z;

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
	struct psmv_vec3_i32 gyro_bias_0;
	struct psmv_vec3_i32 gyro_bias_1;
	struct psmv_vec3_i32 gyro_rot_x;
	struct psmv_vec3_i32 gyro_rot_y;
	struct psmv_vec3_i32 gyro_rot_z;

	struct xrt_vec3 unknown_vec3;
	float unknown_float_0, unknown_float_1;

	bool print_spew;
	bool print_debug;
};


/*
 *
 * Internal functions.
 *
 */

static inline struct psmv_device *
psmv_device(struct xrt_device *xdev)
{
	return (struct psmv_device *)xdev;
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
psmv_i32_from_i16_wire(int32_t *to, const struct psmv_i16_wire *from)
{
	*to = (from->low | from->high << 8) - 0x8000;
}

static void
psmv_vec3_i32_from_i16_wire(struct psmv_vec3_i32 *to,
                            const struct psmv_vec3_i16_wire *from)
{
	psmv_i32_from_i16_wire(&to->x, &from->x);
	psmv_i32_from_i16_wire(&to->y, &from->y);
	psmv_i32_from_i16_wire(&to->z, &from->z);
}

static void
psmv_f32_from_wire(float *to, const struct psmv_f32_wire *from)
{
	uint32_t v = (from->val[0] << 0) | (from->val[1] << 8) |
	             (from->val[2] << 16) | (from->val[3] << 24);
	*to = *((float *)&v);
}

static void
psmv_vec3_f32_from_wire(struct xrt_vec3 *to,
                        const struct psmv_vec3_f32_wite *from)
{
	psmv_f32_from_wire(&to->x, &from->x);
	psmv_f32_from_wire(&to->y, &from->y);
	psmv_f32_from_wire(&to->z, &from->z);
}

static int
psmv_read_hid(struct psmv_device *psmv)
{
	union {
		uint8_t buffer[256];
		struct psmv_get_input input;
	} data;

	struct psmv_parsed_input input = {0};

	do {
		int ret = os_hid_read(psmv->hid, data.buffer, sizeof(data), 0);
		if (ret <= 0) {
			return ret;
		}

		input.battery = data.input.battery;
		input.seq_no = data.input.buttons[3] & 0x0f;

		input.buttons |= data.input.buttons[0] << 24;
		input.buttons |= data.input.buttons[1] << 16;
		input.buttons |= data.input.buttons[2] << 8;
		input.buttons |= data.input.buttons[3] & 0xf0;
		input.timestamp |= data.input.timestamp_low;
		input.timestamp |= data.input.timestamp_high << 8;

		input.frame[0].trigger = data.input.trigger_f1;
		psmv_vec3_i32_from_i16_wire(&input.frame[0].accel,
		                            &data.input.accel_f1);
		psmv_vec3_i32_from_i16_wire(&input.frame[0].gyro,
		                            &data.input.gyro_f1);
		input.frame[1].trigger = data.input.trigger_f2;
		psmv_vec3_i32_from_i16_wire(&input.frame[1].accel,
		                            &data.input.accel_f2);
		psmv_vec3_i32_from_i16_wire(&input.frame[1].gyro,
		                            &data.input.gyro_f2);

		int32_t diff = input.timestamp - psmv->last.timestamp;
		bool missed = input.seq_no != ((psmv->last.seqno + 1) & 0x0f);

		psmv->last.trigger = input.frame[1].trigger;
		psmv->last.timestamp = input.timestamp;
		psmv->last.seqno = input.seq_no;
		psmv->last.buttons = input.buttons;


		PSMV_SPEW(psmv,
		          "\n\t"
		          "missed: %s\n\t"
		          "buttons: %08x\n\t"
		          "battery: %x\n\t"
		          "frame[0].trigger: %02x\n\t"
		          "frame[0].accel_x: %i\n\t"
		          "frame[0].accel_y: %i\n\t"
		          "frame[0].accel_z: %i\n\t"
		          "frame[0].gyro_x: %i\n\t"
		          "frame[0].gyro_y: %i\n\t"
		          "frame[0].gyro_z: %i\n\t"
		          "frame[1].trigger: %02x\n\t"
		          "timestamp: %i\n\t"
		          "diff: %i\n\t"
		          "seq_no: %x\n",
		          missed ? "yes" : "no", input.buttons, input.battery,
		          input.frame[0].trigger, input.frame[0].accel.x,
		          input.frame[0].accel.y, input.frame[0].accel.z,
		          input.frame[0].gyro.x, input.frame[0].gyro.y,
		          input.frame[0].gyro.z, input.frame[1].trigger,
		          input.timestamp, diff, input.seq_no);
	} while (true);

	return 0;
}

static int
psmv_send_led_control(struct psmv_device *psmv,
                      uint8_t red,
                      uint8_t green,
                      uint8_t blue,
                      uint8_t rumble)
{
	struct psmv_set_led msg;
	U_ZERO(&msg);
	msg.id = 0x02;
	msg.red = red;
	msg.green = green;
	msg.blue = blue;
	msg.rumble = rumble;

	return os_hid_write(psmv->hid, (uint8_t *)&msg, sizeof(msg));
}

static void
psmv_update_input_click(struct psmv_device *psmv,
                        int index,
                        int64_t now,
                        uint32_t bit)
{
	psmv->base.inputs[index].timestamp = now;
	psmv->base.inputs[index].value.boolean =
	    (psmv->last.buttons & bit) != 0;
}

static void
psmv_update_trigger_value(struct psmv_device *psmv, int index, int64_t now)
{
	psmv->base.inputs[index].timestamp = now;
	psmv->base.inputs[index].value.vec1.x = psmv->last.trigger / 255.0f;
}

static void
psmv_led_and_trigger_update(struct psmv_device *psmv, int64_t time)
{
	// Need to keep sending led control packets to keep the leds on.
	if (psmv->resend_time > time) {
		return;
	}

	psmv->resend_time = time + 1000000000;
	psmv_send_led_control(psmv, psmv->state.red, psmv->state.green,
	                      psmv->state.blue, psmv->state.rumble);
}

static void
psmv_force_led_and_rumble_update(struct psmv_device *psmv, int64_t time)
{
	// Force a resend;
	psmv->resend_time = 0;
	psmv_led_and_trigger_update(psmv, time);
}

static int
psmv_get_calibration(struct psmv_device *psmv)
{
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
			return ret;
		}

		if (ret != (int)sizeof(part)) {
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
		default: return -1;
		}

		memcpy(dst + dst_offset, src + src_offset,
		       sizeof(part) - src_offset);
	}

	psmv_vec3_i32_from_i16_wire(&psmv->accel_min_x, &data.accel_min_x);
	psmv_vec3_i32_from_i16_wire(&psmv->accel_max_x, &data.accel_max_x);
	psmv_vec3_i32_from_i16_wire(&psmv->accel_min_y, &data.accel_min_y);
	psmv_vec3_i32_from_i16_wire(&psmv->accel_max_y, &data.accel_max_y);
	psmv_vec3_i32_from_i16_wire(&psmv->accel_min_z, &data.accel_min_z);
	psmv_vec3_i32_from_i16_wire(&psmv->accel_max_z, &data.accel_max_z);
	psmv_vec3_i32_from_i16_wire(&psmv->gyro_bias_0, &data.gyro_bias_0);
	psmv_vec3_i32_from_i16_wire(&psmv->gyro_bias_1, &data.gyro_bias_1);
	psmv_vec3_i32_from_i16_wire(&psmv->gyro_rot_x, &data.gyro_rot_x);
	psmv_vec3_i32_from_i16_wire(&psmv->gyro_rot_y, &data.gyro_rot_y);
	psmv_vec3_i32_from_i16_wire(&psmv->gyro_rot_z, &data.gyro_rot_z);
	psmv_vec3_f32_from_wire(&psmv->gyro_fact, &data.gyro_fact);
	psmv_vec3_f32_from_wire(&psmv->unknown_vec3, &data.unknown_vec3);
	psmv_f32_from_wire(&psmv->unknown_float_0, &data.unknown_float_0);
	psmv_f32_from_wire(&psmv->unknown_float_1, &data.unknown_float_1);

	PSMV_DEBUG(
	    psmv,
	    "Calibration:\n"
	    "\taccel_min_x: %i %i %i\n"
	    "\taccel_max_x: %i %i %i\n"
	    "\taccel_min_y: %i %i %i\n"
	    "\taccel_max_y: %i %i %i\n"
	    "\taccel_min_z: %i %i %i\n"
	    "\taccel_max_z: %i %i %i\n"
	    "\tgyro_bias_0: %i %i %i\n"
	    "\tgyro_bias_1: %i %i %i\n"
	    "\tgyro_fact: %f %f %f\n"
	    "\tgyro_rot_x: %i %i %i\n"
	    "\tgyro_rot_y: %i %i %i\n"
	    "\tgyro_rot_z: %i %i %i\n"
	    "\tunknown_vec3: %f %f %f\n"
	    "\tunknown_float_0 %f\n"
	    "\tunknown_float_1 %f\n",
	    psmv->accel_min_x.x, psmv->accel_min_x.y, psmv->accel_min_x.z,
	    psmv->accel_max_x.x, psmv->accel_max_x.y, psmv->accel_max_x.z,
	    psmv->accel_min_y.x, psmv->accel_min_y.y, psmv->accel_min_y.z,
	    psmv->accel_max_y.x, psmv->accel_max_y.y, psmv->accel_max_y.z,
	    psmv->accel_min_z.x, psmv->accel_min_z.y, psmv->accel_min_z.z,
	    psmv->accel_max_z.x, psmv->accel_max_z.y, psmv->accel_max_z.z,
	    psmv->gyro_bias_0.x, psmv->gyro_bias_0.y, psmv->gyro_bias_0.z,
	    psmv->gyro_bias_1.x, psmv->gyro_bias_1.y, psmv->gyro_bias_1.z,
	    psmv->gyro_fact.x, psmv->gyro_fact.y, psmv->gyro_fact.z,
	    psmv->gyro_rot_x.x, psmv->gyro_rot_x.y, psmv->gyro_rot_x.z,
	    psmv->gyro_rot_y.x, psmv->gyro_rot_y.y, psmv->gyro_rot_y.z,
	    psmv->gyro_rot_z.x, psmv->gyro_rot_z.y, psmv->gyro_rot_z.z,
	    psmv->unknown_vec3.x, psmv->unknown_vec3.y, psmv->unknown_vec3.z,
	    psmv->unknown_float_0, psmv->unknown_float_1);

	return 0;
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

	if (psmv->hid != NULL) {
		psmv_send_led_control(psmv, 0x00, 0x00, 0x00, 0x00);

		os_hid_destroy(psmv->hid);
		psmv->hid = NULL;
	}

	free(psmv);
}

static void
psmv_device_update_inputs(struct xrt_device *xdev,
                          struct time_state *timekeeping)
{
	struct psmv_device *psmv = psmv_device(xdev);

	psmv_read_hid(psmv);

	int64_t now = time_state_get_now(timekeeping);

	psmv_led_and_trigger_update(psmv, now);

	// clang-format off
	psmv_update_input_click(psmv, PSMV_INDEX_PS_CLICK, now, PSMV_BUTTON_BIT_PS);
	psmv_update_input_click(psmv, PSMV_INDEX_MOVE_CLICK, now, PSMV_BUTTON_BIT_MOVE_ANY);
	psmv_update_input_click(psmv, PSMV_INDEX_START_CLICK, now, PSMV_BUTTON_BIT_START);
	psmv_update_input_click(psmv, PSMV_INDEX_SELECT_CLICK, now, PSMV_BUTTON_BIT_SELECT);
	psmv_update_input_click(psmv, PSMV_INDEX_SQUARE_CLICK, now, PSMV_BUTTON_BIT_SQUARE);
	psmv_update_input_click(psmv, PSMV_INDEX_CROSS_CLICK, now, PSMV_BUTTON_BIT_CROSS);
	psmv_update_input_click(psmv, PSMV_INDEX_CIRCLE_CLICK, now, PSMV_BUTTON_BIT_CIRCLE);
	psmv_update_input_click(psmv, PSMV_INDEX_TRIANGLE_CLICK, now, PSMV_BUTTON_BIT_TRIANGLE);
	psmv_update_trigger_value(psmv, PSMV_INDEX_TRIGGER_VALUE, now);
	// clang-format on
}

static void
psmv_device_get_tracked_pose(struct xrt_device *xdev,
                             enum xrt_input_name name,
                             struct time_state *timekeeping,
                             int64_t *out_timestamp,
                             struct xrt_space_relation *out_relation)
{
	struct psmv_device *psmv = psmv_device(xdev);

	psmv_read_hid(psmv);
}

static void
psmv_device_set_output(struct xrt_device *xdev,
                       enum xrt_output_name name,
                       struct time_state *timekeeping,
                       union xrt_output_value *value)
{
	struct psmv_device *psmv = psmv_device(xdev);

	if (name != XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION) {
		return;
	}

	psmv->state.rumble =
	    psmv_clamp_zero_to_one_float_to_u8(value->vibration.amplitude);

	// Force a resend;
	int64_t now = time_state_get_now(timekeeping);
	psmv_force_led_and_rumble_update(psmv, now);
}

/*
 *
 * Prober functions.
 *
 */

#define SET_INPUT(NAME)                                                        \
	(psmv->base.inputs[PSMV_INDEX_##NAME].name = XRT_INPUT_PSMV_##NAME)

int
psmv_found(struct xrt_prober *xp,
           struct xrt_prober_device **devices,
           size_t index,
           struct xrt_device **out_xdevs)
{
	struct os_hid_device *hid = NULL;
	int ret;

	// We do not receive any sensor packages on USB.
	if (devices[index]->bus != XRT_BUS_TYPE_BLUETOOTH) {
		return 0;
	}

	ret = xrt_prober_open_hid_interface(xp, devices[index], 0, &hid);
	if (ret != 0) {
		return -1;
	}

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	struct psmv_device *psmv =
	    U_DEVICE_ALLOCATE(struct psmv_device, flags, 12, 1);
	psmv->print_spew = debug_get_bool_option_psmv_spew();
	psmv->print_debug = debug_get_bool_option_psmv_debug();
	psmv->base.destroy = psmv_device_destroy;
	psmv->base.update_inputs = psmv_device_update_inputs;
	psmv->base.get_tracked_pose = psmv_device_get_tracked_pose;
	psmv->base.set_output = psmv_device_set_output;
	psmv->base.name = XRT_DEVICE_PSMV;
	snprintf(psmv->base.str, XRT_DEVICE_NAME_LEN, "%s",
	         "PS Move Controller");
	psmv->hid = hid;
	SET_INPUT(PS_CLICK);
	SET_INPUT(MOVE_CLICK);
	SET_INPUT(START_CLICK);
	SET_INPUT(SELECT_CLICK);
	SET_INPUT(SQUARE_CLICK);
	SET_INPUT(CROSS_CLICK);
	SET_INPUT(CIRCLE_CLICK);
	SET_INPUT(TRIANGLE_CLICK);
	SET_INPUT(TRIGGER_VALUE);
	SET_INPUT(BODY_CENTER_POSE);
	SET_INPUT(BALL_CENTER_POSE);
	SET_INPUT(BALL_TIP_POSE);

	// We only have one output.
	psmv->base.outputs[0].name = XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION;

	static int hack = 0;
	switch (hack++ % 3) {
	case 0: psmv->state.red = 0xff; break;
	case 1:
		psmv->state.red = 0xff;
		psmv->state.blue = 0xff;
		break;
	case 2: psmv->state.blue = 0xff; break;
	}

	// Get calibration data.
	ret = psmv_get_calibration(psmv);
	if (ret != 0) {
		PSMV_ERROR(psmv, "Failed to get calibration data!");
		psmv_device_destroy(&psmv->base);
		return ret;
	}

	// Send the first update package
	psmv_led_and_trigger_update(psmv, 1);

	// Clear any packets
	psmv_read_hid(psmv);

	// And finally done
	*out_xdevs = &psmv->base;

	return 1;
}
