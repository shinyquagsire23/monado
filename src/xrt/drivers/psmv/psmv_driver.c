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
	PSMV_INDEX_X_CLICK,
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
	PSMV_BUTTON_BIT_X           = (1 << 23),

	PSMV_BUTTON_BIT_START       = (1 << 27),
	PSMV_BUTTON_BIT_SELECT      = (1 << 24),

	PSMV_BUTTON_BIT_MOVE_ANY    = PSMV_BUTTON_BIT_MOVE_F1 |
	                              PSMV_BUTTON_BIT_MOVE_F2,
	PSMV_BUTTON_BIT_TRIGGER_ANY = PSMV_BUTTON_BIT_TRIGGER_F1 |
	                              PSMV_BUTTON_BIT_TRIGGER_F1,
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

struct psvm_vec3_16_big_endian
{
	uint8_t low_x;
	uint8_t high_x;
	uint8_t low_y;
	uint8_t high_y;
	uint8_t low_z;
	uint8_t high_z;
};

struct psvm_vec3_32
{
	int32_t x;
	int32_t y;
	int32_t z;
};

struct psmv_get_input
{
	uint8_t header;
	uint8_t buttons[4];
	uint8_t trigger_f1;
	uint8_t trigger_f2;
	uint8_t unknown[4];
	uint8_t timestamp_high;
	uint8_t battery;
	struct psvm_vec3_16_big_endian accel_f1;
	struct psvm_vec3_16_big_endian accel_f2;
	struct psvm_vec3_16_big_endian gyro_f1;
	struct psvm_vec3_16_big_endian gyro_f2;
	uint8_t temp_mag[6];
	uint8_t timestamp_low;
	uint8_t pad[49 - 44];
};

struct psvm_parsed_input
{
	uint32_t buttons;
	uint16_t timestamp;
	uint8_t battery;
	uint8_t seq_no;

	struct
	{
		struct psvm_vec3_32 accel;
		struct psvm_vec3_32 gyro;
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

static void
psmv_vec3_from_16_be_to_32(struct psvm_vec3_32 *to,
                           const struct psvm_vec3_16_big_endian *from)
{
	to->x = (from->low_x | from->high_x << 8) - 0x8000;
	to->y = (from->low_y | from->high_y << 8) - 0x8000;
	to->z = (from->low_z | from->high_z << 8) - 0x8000;
}

static int
psmv_read_hid(struct psmv_device *psmv)
{
	union {
		uint8_t buffer[256];
		struct psmv_get_input input;
	} data;

	struct psvm_parsed_input input = {0};

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
		psmv_vec3_from_16_be_to_32(&input.frame[0].accel,
		                           &data.input.accel_f1);
		psmv_vec3_from_16_be_to_32(&input.frame[0].gyro,
		                           &data.input.gyro_f1);
		input.frame[1].trigger = data.input.trigger_f2;
		psmv_vec3_from_16_be_to_32(&input.frame[1].accel,
		                           &data.input.accel_f2);
		psmv_vec3_from_16_be_to_32(&input.frame[1].gyro,
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
	memset(&msg, 0, sizeof(msg));
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


/*
 *
 * Device functions.
 *
 */

static void
psvm_device_destroy(struct xrt_device *xdev)
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
	psmv_update_input_click(psmv, PSMV_INDEX_X_CLICK, now, PSMV_BUTTON_BIT_X);
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

	float vf = value->vibration.amplitude * 255.0f;

	if (vf >= 255.0f) {
		psmv->state.rumble = 0xff;
	} else if (vf >= 0.0f) {
		psmv->state.rumble = (uint8_t)vf;
	} else {
		psmv->state.rumble = 0x00;
	}

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
           struct xrt_device **out_xdev)
{
	struct os_hid_device *hid = NULL;
	int ret;

	// We do not receive any sensor packages on USB.
	if (devices[index]->bus != XRT_BUS_TYPE_BLUETOOTH) {
		return 0;
	}

	ret = xp->open_hid_interface(xp, devices[index], 0, &hid);
	if (ret != 0) {
		return -1;
	}

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	struct psmv_device *psmv =
	    U_DEVICE_ALLOCATE(struct psmv_device, flags, 12, 1);
	psmv->print_spew = debug_get_bool_option_psmv_spew();
	psmv->print_debug = debug_get_bool_option_psmv_debug();
	psmv->base.destroy = psvm_device_destroy;
	psmv->base.update_inputs = psmv_device_update_inputs;
	psmv->base.get_tracked_pose = psmv_device_get_tracked_pose;
	psmv->base.set_output = psmv_device_set_output;
	snprintf(psmv->base.name, XRT_DEVICE_NAME_LEN, "%s",
	         "PS Move Controller");
	psmv->hid = hid;
	SET_INPUT(PS_CLICK);
	SET_INPUT(MOVE_CLICK);
	SET_INPUT(START_CLICK);
	SET_INPUT(SELECT_CLICK);
	SET_INPUT(SQUARE_CLICK);
	SET_INPUT(X_CLICK);
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

	// Send the first update package
	psmv_led_and_trigger_update(psmv, 1);

	// Clear any packets
	psmv_read_hid(psmv);

	// And finally done
	*out_xdev = &psmv->base;
	return 0;
}
