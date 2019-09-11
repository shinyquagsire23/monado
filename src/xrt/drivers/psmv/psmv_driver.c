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
#include <math.h>

#include "xrt/xrt_prober.h"

#include "math/m_api.h"

#include "util/u_var.h"
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
 * Wire encoding of three 32 bit float, notice order of axis and negation,
 * big endian.
 *
 * @ingroup drv_psmv
 */
struct psmv_vec3_f32_zn_wire
{
	struct psmv_f32_wire x;
	struct psmv_f32_wire z_neg;
	struct psmv_f32_wire y;
};

/*!
 * Wire encoding of three 32 bit float, notice order of axis, big endian.
 *
 * @ingroup drv_psmv
 */
struct psmv_vec3_f32_wire
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
 * Wire encoding of three 16 bit integers, notice order of axis and negation.
 *
 * @ingroup drv_psmv
 */
struct psmv_vec3_i16_zn_wire
{
	struct psmv_i16_wire x;
	struct psmv_i16_wire z_neg;
	struct psmv_i16_wire y;
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
	struct psmv_vec3_i16_zn_wire accel_f1;
	struct psmv_vec3_i16_zn_wire accel_f2;
	struct psmv_vec3_i16_zn_wire gyro_f1;
	struct psmv_vec3_i16_zn_wire gyro_f2;
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
	struct psmv_vec3_i16_zn_wire accel_max_y;
	struct psmv_vec3_i16_zn_wire accel_min_x;
	struct psmv_vec3_i16_zn_wire accel_min_y;
	struct psmv_vec3_i16_zn_wire accel_max_x;
	struct psmv_vec3_i16_zn_wire accel_min_z;
	struct psmv_vec3_i16_zn_wire accel_max_z;
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
	struct psmv_vec3_f32_zn_wire unknown_vec3;
	struct psmv_vec3_f32_wire gyro_fact;
	struct psmv_f32_wire unknown_float_0;
	struct psmv_f32_wire unknown_float_1;
	uint8_t _pad[17];
};

struct psmv_parsed_sample
{
	struct xrt_vec3_i32 accel;
	struct xrt_vec3_i32 gyro;
	uint8_t trigger;
};

struct psmv_parsed_input
{
	uint32_t buttons;
	uint16_t timestamp;
	uint8_t battery;
	uint8_t seq_no;

	struct psmv_parsed_sample sample[2];
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

	struct
	{
		int64_t resend_time;
		struct xrt_colour_rgb_u8 led;
		uint8_t rumble;
	} wants;

	struct
	{
		struct xrt_colour_rgb_u8 led;
		uint8_t rumble;
	} state;

	//! Last sensor read.
	struct psmv_parsed_input last;

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

	struct
	{
		struct xrt_vec3 accel;
		struct xrt_vec3 gyro;
	} read;

	bool print_spew;
	bool print_debug;

	struct
	{
		bool control;
		bool calibration;
		bool last_frame;
	} gui;

	struct
	{
		struct xrt_quat rot;
	} fusion;
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
psmv_i32_from_i16_wire(int32_t *to, const struct psmv_i16_wire *from)
{
	*to = (from->low | from->high << 8) - 0x8000;
}

static void
psmv_i32_from_i16_wire_neg(int32_t *to, const struct psmv_i16_wire *from)
{
	int32_t v;
	psmv_i32_from_i16_wire(&v, from);
	*to = -v;
}

static void
psmv_from_vec3_i16_zn_wire(struct xrt_vec3_i32 *to,
                           const struct psmv_vec3_i16_zn_wire *from)
{
	psmv_i32_from_i16_wire(&to->x, &from->x);
	psmv_i32_from_i16_wire(&to->y, &from->y);
	psmv_i32_from_i16_wire_neg(&to->z, &from->z_neg);
}

static void
psmv_from_vec3_i16_wire(struct xrt_vec3_i32 *to,
                        const struct psmv_vec3_i16_wire *from)
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

	safe_copy.wire = (from->val[0] << 0) | (from->val[1] << 8) |
	                 (from->val[2] << 16) | (from->val[3] << 24);
	*to = safe_copy.f32;
}

static void
psmv_f32_from_wire_neg(float *to, const struct psmv_f32_wire *from)
{
	float v;
	psmv_f32_from_wire(&v, from);
	*to = -v;
}

static void
psmv_from_vec3_f32_zn_wire(struct xrt_vec3 *to,
                           const struct psmv_vec3_f32_zn_wire *from)
{
	psmv_f32_from_wire(&to->x, &from->x);
	psmv_f32_from_wire(&to->y, &from->y);
	psmv_f32_from_wire_neg(&to->z, &from->z_neg);
}

static void
psmv_from_vec32_f32_wire(struct xrt_vec3 *to,
                         const struct psmv_vec3_f32_wire *from)
{
	psmv_f32_from_wire(&to->x, &from->x);
	psmv_f32_from_wire(&to->y, &from->y);
	psmv_f32_from_wire(&to->z, &from->z);
}

#define PSMV_TICK_PERIOD (1.0 / 120.0)

static void
update_fusion(struct psmv_device *psmv, struct psmv_parsed_sample *sample)
{
	struct xrt_vec3 mag = {0.0f, 0.0f, 0.0f};
	float dt = PSMV_TICK_PERIOD;
	(void)mag;
	(void)dt;

	struct xrt_vec3_i32 *ra = &sample->accel;
	struct xrt_vec3_i32 *rg = &sample->gyro;

	//! @todo Pre-calculate this.
	double ax = (psmv->accel_max_x.x - psmv->accel_min_x.x) / 2.0;
	double ay = (psmv->accel_max_y.y - psmv->accel_min_y.y) / 2.0;
	double az = (psmv->accel_max_z.z - psmv->accel_min_z.z) / 2.0;

	double bx = (psmv->accel_min_y.x + psmv->accel_max_y.x +
	             psmv->accel_min_z.x + psmv->accel_max_z.x) /
	            -4.0;
	double by = (psmv->accel_min_x.y + psmv->accel_max_x.y +
	             psmv->accel_min_z.y + psmv->accel_max_z.y) /
	            -4.0;
	double bz = (psmv->accel_min_x.z + psmv->accel_max_x.z +
	             psmv->accel_min_y.z + psmv->accel_max_y.z) /
	            -4.0;

	psmv->read.accel.x = (ra->x + bx) / ax;
	psmv->read.accel.y = (ra->y + by) / ay;
	psmv->read.accel.z = (ra->z + bz) / az;

	double gx =
	    (psmv->gyro_rot_x.x - (psmv->gyro_bias_0.x * psmv->gyro_fact.x));
	double gy =
	    (psmv->gyro_rot_y.y - (psmv->gyro_bias_0.y * psmv->gyro_fact.y));
	double gz =
	    (psmv->gyro_rot_z.z - (psmv->gyro_bias_0.z * psmv->gyro_fact.z));

	gx = (2.0 * M_PI * 80.0) / (60.0 * gx);
	gy = (2.0 * M_PI * 80.0) / (60.0 * gy);
	gz = (2.0 * M_PI * 80.0) / (60.0 * gz);

	psmv->read.gyro.x = rg->x * gx;
	psmv->read.gyro.y = rg->y * gy;
	psmv->read.gyro.z = rg->z * gz;

	// Super simple fusion.
	math_quat_integrate_velocity(&psmv->fusion.rot, &psmv->read.gyro, dt,
	                             &psmv->fusion.rot);
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

		input.sample[0].trigger = data.input.trigger_f1;
		psmv_from_vec3_i16_zn_wire(&input.sample[0].accel,
		                           &data.input.accel_f1);
		psmv_from_vec3_i16_zn_wire(&input.sample[0].gyro,
		                           &data.input.gyro_f1);
		input.sample[1].trigger = data.input.trigger_f2;
		psmv_from_vec3_i16_zn_wire(&input.sample[1].accel,
		                           &data.input.accel_f2);
		psmv_from_vec3_i16_zn_wire(&input.sample[1].gyro,
		                           &data.input.gyro_f2);

		uint32_t diff = psmv_calc_delta_and_handle_rollover(
		    input.timestamp, psmv->last.timestamp);
		bool missed = input.seq_no != ((psmv->last.seq_no + 1) & 0x0f);

		// Update timestamp.
		psmv->last = input;

		PSMV_SPEW(psmv,
		          "\n\t"
		          "missed: %s\n\t"
		          "buttons: %08x\n\t"
		          "battery: %x\n\t"
		          "sample[0].trigger: %02x\n\t"
		          "sample[0].accel_x: %i\n\t"
		          "sample[0].accel_y: %i\n\t"
		          "sample[0].accel_z: %i\n\t"
		          "sample[0].gyro_x: %i\n\t"
		          "sample[0].gyro_y: %i\n\t"
		          "sample[0].gyro_z: %i\n\t"
		          "sample[1].trigger: %02x\n\t"
		          "timestamp: %i\n\t"
		          "diff: %i\n\t"
		          "seq_no: %x\n",
		          missed ? "yes" : "no", input.buttons, input.battery,
		          input.sample[0].trigger, input.sample[0].accel.x,
		          input.sample[0].accel.y, input.sample[0].accel.z,
		          input.sample[0].gyro.x, input.sample[0].gyro.y,
		          input.sample[0].gyro.z, input.sample[1].trigger,
		          input.timestamp, diff, input.seq_no);

		// Process the parsed data.
		update_fusion(psmv, &input.sample[0]);
		update_fusion(psmv, &input.sample[1]);

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
	psmv->base.inputs[index].value.vec1.x =
	    psmv->last.sample[1].trigger / 255.0f;
}

static void
psmv_led_and_trigger_update(struct psmv_device *psmv, int64_t time)
{
	// Need to keep sending led control packets to keep the leds on.
	if (psmv->wants.resend_time > time &&
	    psmv->state.led.r == psmv->wants.led.r &&
	    psmv->state.led.g == psmv->wants.led.g &&
	    psmv->state.led.b == psmv->wants.led.b &&
	    psmv->state.rumble == psmv->wants.rumble) {
		return;
	}

	psmv->state.led.r = psmv->wants.led.r;
	psmv->state.led.g = psmv->wants.led.g;
	psmv->state.led.b = psmv->wants.led.b;
	psmv->state.rumble = psmv->wants.rumble;

	psmv->wants.resend_time = time + 1000000000;
	psmv_send_led_control(psmv, psmv->state.led.r, psmv->state.led.g,
	                      psmv->state.led.b, psmv->state.rumble);
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

	psmv_from_vec3_i16_zn_wire(&psmv->accel_min_x, &data.accel_min_x);
	psmv_from_vec3_i16_zn_wire(&psmv->accel_max_x, &data.accel_max_x);
	psmv_from_vec3_i16_zn_wire(&psmv->accel_min_y, &data.accel_min_y);
	psmv_from_vec3_i16_zn_wire(&psmv->accel_max_y, &data.accel_max_y);
	psmv_from_vec3_i16_zn_wire(&psmv->accel_min_z, &data.accel_min_z);
	psmv_from_vec3_i16_zn_wire(&psmv->accel_max_z, &data.accel_max_z);
	psmv_from_vec3_i16_wire(&psmv->gyro_bias_0, &data.gyro_bias_0);
	psmv_from_vec3_i16_wire(&psmv->gyro_bias_1, &data.gyro_bias_1);
	psmv_from_vec3_i16_wire(&psmv->gyro_rot_x, &data.gyro_rot_x);
	psmv_from_vec3_i16_wire(&psmv->gyro_rot_y, &data.gyro_rot_y);
	psmv_from_vec3_i16_wire(&psmv->gyro_rot_z, &data.gyro_rot_z);
	psmv_from_vec32_f32_wire(&psmv->gyro_fact, &data.gyro_fact);
	psmv_from_vec3_f32_zn_wire(&psmv->unknown_vec3, &data.unknown_vec3);
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

	// Remove the variable tracking.
	u_var_remove_root(psmv);

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

	out_relation->pose.orientation = psmv->fusion.rot;

	//! @todo assuming that orientation is actually currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

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

	psmv->wants.rumble =
	    psmv_clamp_zero_to_one_float_to_u8(value->vibration.amplitude);

	// Resend if the rumble has been changed.
	int64_t now = time_state_get_now(timekeeping);
	psmv_led_and_trigger_update(psmv, now);
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
	psmv->fusion.rot.w = 1.0f;
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
	case 0: psmv->wants.led.r = 0xff; break;
	case 1:
		psmv->wants.led.r = 0xff;
		psmv->wants.led.b = 0xff;
		break;
	case 2: psmv->wants.led.b = 0xff; break;
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

	// Start the variable tracking now that everything is in place.
	// clang-format off
	u_var_add_root(psmv, "PSMV Controller", true);
	u_var_add_gui_header(psmv, &psmv->gui.calibration, "Calibration");
	u_var_add_vec3_i32(psmv, &psmv->accel_min_x, "accel_min_x");
	u_var_add_vec3_i32(psmv, &psmv->accel_max_x, "accel_max_x");
	u_var_add_vec3_i32(psmv, &psmv->accel_min_y, "accel_min_y");
	u_var_add_vec3_i32(psmv, &psmv->accel_max_y, "accel_max_y");
	u_var_add_vec3_i32(psmv, &psmv->accel_min_z, "accel_min_z");
	u_var_add_vec3_i32(psmv, &psmv->accel_max_z, "accel_max_z");
	u_var_add_vec3_i32(psmv, &psmv->gyro_rot_x, "gyro_rot_x");
	u_var_add_vec3_i32(psmv, &psmv->gyro_rot_y, "gyro_rot_y");
	u_var_add_vec3_i32(psmv, &psmv->gyro_rot_z, "gyro_rot_z");
	u_var_add_vec3_i32(psmv, &psmv->gyro_bias_0, "gyro_bias_0");
	u_var_add_vec3_i32(psmv, &psmv->gyro_bias_1, "gyro_bias_1");
	u_var_add_vec3_f32(psmv, &psmv->gyro_fact, "gyro_fact");
	u_var_add_gui_header(psmv, &psmv->gui.last_frame, "Last data");
	u_var_add_ro_vec3_i32(psmv, &psmv->last.sample[0].accel, "last.sample[0].accel");
	u_var_add_ro_vec3_i32(psmv, &psmv->last.sample[1].accel, "last.sample[1].accel");
	u_var_add_ro_vec3_i32(psmv, &psmv->last.sample[0].gyro, "last.sample[0].gyro");
	u_var_add_ro_vec3_i32(psmv, &psmv->last.sample[1].gyro, "last.sample[1].gyro");
	u_var_add_ro_vec3_f32(psmv, &psmv->read.accel, "read.accel");
	u_var_add_ro_vec3_f32(psmv, &psmv->read.gyro, "read.gyro");
	u_var_add_gui_header(psmv, &psmv->gui.control, "Control");
	u_var_add_rgb_u8(psmv, &psmv->wants.led, "Led");
	u_var_add_u8(psmv, &psmv->wants.rumble, "Rumble");
	u_var_add_bool(psmv, &psmv->print_debug, "Debug");
	u_var_add_bool(psmv, &psmv->print_spew, "Spew");
	// clang-format on

	// And finally done
	*out_xdevs = &psmv->base;

	return 1;
}
