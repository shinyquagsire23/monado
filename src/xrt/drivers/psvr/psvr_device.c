// Copyright 2016, Joey Ferwerda.
// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PSVR device implementation, imported from OpenHMD.
 * @author Joey Ferwerda <joeyferweda@gmail.com>
 * @author Philipp Zabel <philipp.zabel@gmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_psvr
 */

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_tracking.h"

#include "os/os_time.h"
#include "os/os_threading.h"

#include "math/m_api.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_distortion_mesh.h"

#include "math/m_imu_3dof.h"

#include "math/m_mathinclude.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psvr_device.h"


/*
 *
 * Structs and defines.
 *
 */

DEBUG_GET_ONCE_BOOL_OPTION(psvr_disco, "PSVR_DISCO", false)
#define PSVR_DEBUG(p, ...) U_LOG_XDEV_IFL_D(&p->base, p->log_level, __VA_ARGS__)
#define PSVR_ERROR(p, ...) U_LOG_XDEV_IFL_E(&p->base, p->log_level, __VA_ARGS__)

#define FEATURE_BUFFER_SIZE 256

/*!
 * Private struct for the @ref drv_psvr device.
 *
 * @ingroup drv_psvr
 * @implements xrt_device
 */
struct psvr_device
{
	struct xrt_device base;

	hid_device *hid_sensor;
	hid_device *hid_control;
	struct os_mutex device_mutex;

	struct xrt_tracked_psvr *tracker;

	timepoint_ns last_sensor_time;

	struct psvr_parsed_sensor last;

	struct
	{
		uint8_t leds[9];
	} wants;

	struct
	{
		uint8_t leds[9];
	} state;

	struct
	{
		struct xrt_vec3 gyro;
		struct xrt_vec3 accel;
	} read;

	uint16_t buttons;

	bool powered_on;
	bool in_vr_mode;

	enum u_logging_level log_level;

	struct
	{
		union {
			uint8_t data[290];
			struct
			{
				uint32_t _pad0[4];
				struct xrt_vec3 unknown0;
				uint32_t _zero0;
				uint32_t _pad2_vec3_zero[4];
				uint32_t _pad3_vec3_zero[4];
				uint32_t _pad4_vec3_zero[4];
				struct xrt_vec3 accel_pos_y;
				uint32_t _pad5[1];
				struct xrt_vec3 accel_neg_x;
				uint32_t _pad6[1];
				struct xrt_vec3 accel_neg_y;
				uint32_t _pad7[1];
				struct xrt_vec3 accel_pos_x;
				uint32_t _pad8[1];
				struct xrt_vec3 accel_pos_z;
				uint32_t _pad9[1];
				struct xrt_vec3 accel_neg_z;
				uint32_t _pad10[1];
				struct xrt_vec3 gyro_neg_y;
				uint32_t _pad11[1];
				struct xrt_vec3 gyro_pos_x;
				uint32_t _pad12[1];
				struct xrt_vec3 gyro_neg_z;
				uint32_t _pad13[1];
			};
		};
		int last_packet;
	} calibration;


	struct
	{
		bool last_frame;
		bool control;
	} gui;

#if 1
	struct m_imu_3dof fusion;
#else
	struct
	{
		struct xrt_quat rot;
	} fusion;
#endif

	//! For compute_distortion
	struct u_panotools_values vals;
};


// Alternative way to turn on all of the leds.
XRT_MAYBE_UNUSED static const unsigned char psvr_tracking_on[12] = {
    0x11, 0x00, 0xaa, 0x08, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
};


#define PSVR_LED_POWER_OFF ((uint8_t)0x00)
#define PSVR_LED_POWER_MAX ((uint8_t)0xff)

#define PSVR_LED_POWER_WIRE_OFF ((uint8_t)0)
#define PSVR_LED_POWER_WIRE_MAX ((uint8_t)100)


enum psvr_leds
{
	PSVR_LED_A = (1 << 0),
	PSVR_LED_B = (1 << 1),
	PSVR_LED_C = (1 << 2),
	PSVR_LED_D = (1 << 3),
	PSVR_LED_E = (1 << 4),
	PSVR_LED_F = (1 << 5),
	PSVR_LED_G = (1 << 6),
	PSVR_LED_H = (1 << 7),
	PSVR_LED_I = (1 << 8),

	PSVR_LED_FRONT = PSVR_LED_A | PSVR_LED_B | PSVR_LED_C | PSVR_LED_D | PSVR_LED_E | PSVR_LED_F | PSVR_LED_G,

	PSVR_LED_BACK = PSVR_LED_H | PSVR_LED_I,

	PSVR_LED_ALL = PSVR_LED_FRONT | PSVR_LED_BACK,
};


/*
 *
 * Helpers and internal functions.
 *
 */

static inline struct psvr_device *
psvr_device(struct xrt_device *p)
{
	return (struct psvr_device *)p;
}

static int
open_hid(struct psvr_device *p, struct hid_device_info *dev_info, hid_device **out_dev)
{
	hid_device *dev = NULL;
	int ret;

	dev = hid_open_path(dev_info->path);
	if (dev == NULL) {
		PSVR_ERROR(p, "Failed to open '%s'", dev_info->path);
		return -1;
	}

	ret = hid_set_nonblocking(dev, 1);
	if (ret != 0) {
		PSVR_ERROR(p, "Failed to set non-blocking on device");
		hid_close(dev);
		return -1;
	}

	*out_dev = dev;
	return 0;
}

static int
send_to_control(struct psvr_device *psvr, const uint8_t *data, size_t size)
{
	return hid_write(psvr->hid_control, data, size);
}

static int
send_request_data(struct psvr_device *psvr, uint8_t id, uint8_t num)
{
	const uint8_t data[12] = {
	    0x81, 0x00, 0xaa, 0x08, id, num, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	return send_to_control(psvr, data, sizeof(data));
}


/*
 *
 * Packet reading code.
 *
 */

static uint8_t
scale_led_power(uint8_t power)
{
	return (uint8_t)((power / 255.0f) * 100.0f);
}

static void
read_sample_and_apply_calibration(struct psvr_device *psvr,
                                  struct psvr_parsed_sample *sample,
                                  struct xrt_vec3 *out_accel,
                                  struct xrt_vec3 *out_gyro)
{
	const struct xrt_vec3_i32 raw_accel = sample->accel;
	const struct xrt_vec3_i32 raw_gyro = sample->gyro;

	// Convert so that for a perfect IMU 1.0 is one G.
	struct xrt_vec3 accel = {
	    raw_accel.x / 16384.0f,
	    raw_accel.y / 16384.0f,
	    raw_accel.z / 16384.0f,
	};

	// What unit is this?
	struct xrt_vec3 gyro = {
	    raw_gyro.x * 0.00105f,
	    raw_gyro.y * 0.00105f,
	    raw_gyro.z * 0.00105f,
	};

	float ax = 2.0 / (psvr->calibration.accel_pos_x.x - psvr->calibration.accel_neg_x.x);
	float ay = 2.0 / (psvr->calibration.accel_pos_y.y - psvr->calibration.accel_neg_y.y);
	float az = 2.0 / (psvr->calibration.accel_pos_z.z - psvr->calibration.accel_neg_z.z);

	float ox = (psvr->calibration.accel_pos_x.x + psvr->calibration.accel_neg_x.x) / 2.0;
	float oy = (psvr->calibration.accel_pos_y.y + psvr->calibration.accel_neg_y.y) / 2.0;
	float oz = (psvr->calibration.accel_pos_z.z + psvr->calibration.accel_neg_z.z) / 2.0;

	accel.x -= ox;
	accel.y -= oy;
	accel.z -= oz;
	accel.x *= ax;
	accel.y *= ay;
	accel.z *= az;

	// Go from Gs to m/s2 and flip the Z-axis.
	accel.x *= +MATH_GRAVITY_M_S2;
	accel.y *= +MATH_GRAVITY_M_S2;
	accel.z *= -MATH_GRAVITY_M_S2;

	// Flip the Z-axis.
	gyro.x *= +1.0;
	gyro.y *= +1.0;
	gyro.z *= -1.0;

	*out_accel = accel;
	*out_gyro = gyro;
}

static void
update_fusion(struct psvr_device *psvr,
              struct psvr_parsed_sample *sample,
              uint32_t tick_delta,
              timepoint_ns timestamp_ns)
{
	struct xrt_vec3 mag = {0.0f, 0.0f, 0.0f};
	(void)mag;

	read_sample_and_apply_calibration(psvr, sample, &psvr->read.accel, &psvr->read.gyro);

	if (psvr->tracker != NULL) {
		struct xrt_tracking_sample sample;
		sample.accel_m_s2 = psvr->read.accel;
		sample.gyro_rad_secs = psvr->read.gyro;

		xrt_tracked_psvr_push_imu(psvr->tracker, timestamp_ns, &sample);
	} else {
#if 1
		timepoint_ns now = os_monotonic_get_ns();
		m_imu_3dof_update(&psvr->fusion, now, &psvr->read.accel, &psvr->read.gyro);
#else
		float delta_secs = tick_delta / PSVR_TICKS_PER_SECOND;

		math_quat_integrate_velocity(&psvr->fusion.rot, &psvr->read.gyro, delta_secs, &psvr->fusion.rot);
#endif
	}
}

static uint32_t
calc_delta_and_handle_rollover(uint32_t next, uint32_t last)
{
	uint32_t tick_delta = next - last;

	// The 24-bit tick counter has rolled over,
	// adjust the "negative" value to be positive.
	if (tick_delta > 0xffffff) {
		tick_delta += 0x1000000;
	}

	return tick_delta;
}

static void
handle_tracker_sensor_msg(struct psvr_device *psvr, unsigned char *buffer, int size)
{
	timepoint_ns now = os_monotonic_get_ns();
	uint32_t last_sample_tick = psvr->last.samples[1].tick;

	if (!psvr_parse_sensor_packet(&psvr->last, buffer, size)) {
		PSVR_ERROR(psvr, "couldn't decode tracker sensor message");
	}

	struct psvr_parsed_sensor *s = &psvr->last;

	// Simplest is the buttons.
	psvr->buttons = s->buttons;

	uint32_t tick_delta = 500;

	// Startup correction, ignore last_sample_tick if zero.
	if (last_sample_tick > 0) {
		tick_delta = calc_delta_and_handle_rollover(s->samples[0].tick, last_sample_tick);

		// The PSVR device can buffer sensor data from previous
		// sessions which we can get at the start of new sessions.
		// @todo Maybe just skip the first 10 sensor packets?
		// @todo Maybe reset sensor fusion?
		if (tick_delta < 400 || tick_delta > 600) {
			PSVR_DEBUG(psvr, "tick_delta = %u", tick_delta);
			tick_delta = 500;
		}
	}
	// New delta between the two samples.
	uint32_t tick_delta2 = calc_delta_and_handle_rollover(s->samples[1].tick, s->samples[0].tick);

	time_duration_ns inter_sample_duration_ns = tick_delta2 * PSVR_NS_PER_TICK;
	// Update the fusion with first sample.
	update_fusion(psvr, &s->samples[0], tick_delta, now - inter_sample_duration_ns);

	// Update the fusion with second sample.
	update_fusion(psvr, &s->samples[1], tick_delta2, now);
	psvr->last_sensor_time = now;
}

static void
handle_control_status_msg(struct psvr_device *psvr, unsigned char *buffer, int size)
{
	struct psvr_parsed_status status = {0};

	if (!psvr_parse_status_packet(&status, buffer, size)) {
		PSVR_ERROR(psvr, "couldn't decode tracker sensor message");
	}


	/*
	 * Power
	 */

	if (status.status & PSVR_STATUS_BIT_POWER) {
		if (!psvr->powered_on) {
			PSVR_DEBUG(psvr, "Device powered on! '%02x'", status.status);
		}
		psvr->powered_on = true;
	} else {
		if (psvr->powered_on) {
			PSVR_DEBUG(psvr, "Device powered off! '%02x'", status.status);
		}
		psvr->powered_on = false;
	}


	/*
	 * VR-Mode
	 */

	if (status.vr_mode == PSVR_STATUS_VR_MODE_OFF) {
		if (psvr->in_vr_mode) {
			PSVR_DEBUG(psvr, "Device not in vr-mode! '%02x'", status.vr_mode);
		}
		psvr->in_vr_mode = false;
	} else if (status.vr_mode == PSVR_STATUS_VR_MODE_ON) {
		if (!psvr->in_vr_mode) {
			PSVR_DEBUG(psvr, "Device in vr-mode! '%02x'", status.vr_mode);
		}
		psvr->in_vr_mode = true;
	} else {
		PSVR_ERROR(psvr, "Unknown vr_mode status!");
	}
}

static void
handle_device_name_msg(struct psvr_device *psvr, unsigned char *buffer, int size)
{
	//! @todo Get the name here.
}

static void
handle_calibration_msg(struct psvr_device *psvr, const unsigned char *buffer, size_t size)
{
	const size_t data_start = 6;
	const size_t data_length = 58;
	const size_t packet_length = data_start + data_length;

	if (size != packet_length) {
		PSVR_ERROR(psvr, "invalid calibration packet length");
		return;
	}

	size_t which = buffer[1];
	size_t dst = data_length * which;
	for (size_t src = data_start; src < size; src++, dst++) {
		psvr->calibration.data[dst] = buffer[src];
	}

	psvr->calibration.last_packet = which;
}

static void
handle_control_0x82(struct psvr_device *psvr, unsigned char *buffer, int size)
{
	if (size < 4) {
		return;
	}

	if (size < (int)sizeof(float) * 6) {
		PSVR_DEBUG(psvr, "%02x %02x %02x %02x", buffer[0], buffer[1], buffer[2], buffer[3]);
	}

	float *f = (float *)buffer;
	int *i = (int *)buffer;

	PSVR_DEBUG(psvr,
	           "%02x %02x %02x %02x\n"
	           "%+f %+f %+f %+f %+f\n"
	           "0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n"
	           "% 10i % 10i % 10i % 10i % 10i",
	           buffer[0], buffer[1], buffer[2], buffer[3], f[1], f[2], f[3], f[4], f[5], i[1], i[2], i[3], i[4],
	           i[5], i[1], i[2], i[3], i[4], i[5]);
}

static void
handle_control_0xA0(struct psvr_device *psvr, unsigned char *buffer, int size)
{
	if (size < 4) {
		return;
	}

	PSVR_DEBUG(psvr, "%02x %02x %02x %02x", buffer[0], buffer[1], buffer[2], buffer[3]);
}

static int
read_sensor_packets(struct psvr_device *psvr)
{
	uint8_t buffer[FEATURE_BUFFER_SIZE];
	int size = 0;

	do {
		size = hid_read(psvr->hid_sensor, buffer, FEATURE_BUFFER_SIZE);
		if (size == 0) {
			return 0;
		}
		if (size < 0) {
			return -1;
		}

		handle_tracker_sensor_msg(psvr, buffer, size);
	} while (true);
}

static int
read_control_packets(struct psvr_device *psvr)
{
	uint8_t buffer[FEATURE_BUFFER_SIZE];
	int size = 0;

	do {
		size = hid_read(psvr->hid_control, buffer, FEATURE_BUFFER_SIZE);
		if (size == 0) {
			return 0;
		}
		if (size < 0) {
			return -1;
		}

		if (buffer[0] == PSVR_PKG_STATUS) {
			handle_control_status_msg(psvr, buffer, size);
		} else if (buffer[0] == PSVR_PKG_DEVICE_NAME) {
			handle_device_name_msg(psvr, buffer, size);
		} else if (buffer[0] == PSVR_PKG_CALIBRATION) {
			handle_calibration_msg(psvr, buffer, size);
		} else if (buffer[0] == PSVR_PKG_0x82) {
			handle_control_0x82(psvr, buffer, size);
		} else if (buffer[0] == PSVR_PKG_0xA0) {
			handle_control_0xA0(psvr, buffer, size);
		} else {
			PSVR_ERROR(psvr, "Got report, 0x%02x", buffer[0]);
		}

	} while (true);
}


/*!
 * Get the device name data and calibration data, see link below for info.
 *
 * https://github.com/gusmanb/PSVRFramework/wiki/Report-0x81-Device-ID-and-Calibration
 */
static int
read_calibration_data(struct psvr_device *psvr)
{
	// Request the device name.
	int ret = send_request_data(psvr, PSVR_GET_DATA_ID_DEVICE_NAME, 0);
	if (ret < 0) {
		return ret;
	}

	// Request unknown data 0x82.
	ret = send_request_data(psvr, PSVR_GET_DATA_ID_0x82, 0);
	if (ret < 0) {
		return ret;
	}

	// There are 5 pages of PSVR calibration data.
	for (int i = 0; i < 5; i++) {
		// Request the IMU calibration data.
		ret = send_request_data(psvr, PSVR_GET_DATA_ID_CALIBRATION, i);
		if (ret < 0) {
			return ret;
		}
	}

	// We have requested 5 pages worth of data, wait for the replies.
	for (int i = 0; i < 100; i++) {
		os_nanosleep(1000 * 1000);
		read_control_packets(psvr);

		// If we have gotten of the packets stop wait.
		if (psvr->calibration.last_packet == 4) {
			break;
		}
	}

	// Did we really get all of the data?
	if (psvr->calibration.last_packet != 4) {
		PSVR_ERROR(psvr, "Failed to get calibration");
		return -1;
	}

	PSVR_DEBUG(psvr,
	           "calibration.accel_pos_x: %f %f %f\n"
	           "calibration.accel_neg_x: %f %f %f\n"
	           "calibration.accel_pos_y: %f %f %f\n"
	           "calibration.accel_neg_y: %f %f %f\n"
	           "calibration.accel_pos_z: %f %f %f\n"
	           "calibration.accel_neg_z: %f %f %f\n",
	           psvr->calibration.accel_pos_x.x, psvr->calibration.accel_pos_x.y, psvr->calibration.accel_pos_x.z,
	           psvr->calibration.accel_neg_x.x, psvr->calibration.accel_neg_x.y, psvr->calibration.accel_neg_x.z,
	           psvr->calibration.accel_pos_y.x, psvr->calibration.accel_pos_y.y, psvr->calibration.accel_pos_y.z,
	           psvr->calibration.accel_neg_y.x, psvr->calibration.accel_neg_y.y, psvr->calibration.accel_neg_y.z,
	           psvr->calibration.accel_pos_z.x, psvr->calibration.accel_pos_z.y, psvr->calibration.accel_pos_z.z,
	           psvr->calibration.accel_neg_z.x, psvr->calibration.accel_neg_z.y, psvr->calibration.accel_neg_z.z);

#if 0
	for (size_t i = 0; i < sizeof(psvr->calibration.data); i++) {
		fprintf(stderr, "%02x ", psvr->calibration.data[i]);
	}
	fprintf(stderr, "\n");

	int *data = (int*)&psvr->calibration.data[0];
	for (size_t i = 0; i < (sizeof(psvr->calibration.data) / 4); i++) {
		int v = data[i];
		U_LOG_E("%i %f", v, *(float*)&v);
	}
#endif

	// Jobs done!
	//  - Orc peon.
	return 0;
}


/*
 *
 * Control sending functions.
 *
 */

static int
wait_for_power(struct psvr_device *psvr, bool on)
{
	for (int i = 0; i < 5000; i++) {
		read_sensor_packets(psvr);
		read_control_packets(psvr);

		if (psvr->powered_on == on) {
			return 0;
		}

		os_nanosleep(1000 * 1000);
	}

	return -1;
}

static int
wait_for_vr_mode(struct psvr_device *psvr, bool on)
{
	for (int i = 0; i < 5000; i++) {
		read_sensor_packets(psvr);
		read_control_packets(psvr);

		if (psvr->in_vr_mode == on) {
			return 0;
		}

		os_nanosleep(1000 * 1000);
	}

	return -1;
}

static int
control_power_and_wait(struct psvr_device *psvr, bool on)
{
	const char *status = on ? "on" : "off";
	const uint8_t data[8] = {
	    0x17, 0x00, 0xaa, 0x04, on, 0x00, 0x00, 0x00,
	};

	int ret = send_to_control(psvr, data, sizeof(data));
	if (ret < 0) {
		PSVR_ERROR(psvr, "Failed to switch %s the headset! '%i'", status, ret);
	}

	ret = wait_for_power(psvr, on);
	if (ret < 0) {
		PSVR_ERROR(psvr, "Failed to wait for headset power %s! '%i'", status, ret);
		return ret;
	}

	return ret;
}

static int
control_vrmode_and_wait(struct psvr_device *psvr, bool on)
{
	const uint8_t data[8] = {
	    0x23, 0x00, 0xaa, 0x04, on, 0x00, 0x00, 0x00,
	};
	int ret;

	ret = send_to_control(psvr, data, sizeof(data));
	if (ret < 0) {
		PSVR_ERROR(psvr, "Failed %s vr-mode the headset! '%i'", on ? "enable" : "disable", ret);
		return ret;
	}

	ret = wait_for_vr_mode(psvr, on);
	if (ret < 0) {
		PSVR_ERROR(psvr, "Failed to wait for vr mode! '%i'", ret);
		return ret;
	}

	return 0;
}

static int
update_leds_if_changed(struct psvr_device *psvr)
{
	if (memcmp(psvr->wants.leds, psvr->state.leds, sizeof(psvr->state.leds)) == 0) {
		return 0;
	}

	memcpy(psvr->state.leds, psvr->wants.leds, sizeof(psvr->state.leds));

	uint8_t data[20] = {
	    0x15,
	    0x00,
	    0xaa,
	    0x10,
	    (uint8_t)PSVR_LED_ALL,
	    (uint8_t)(PSVR_LED_ALL >> 8),
	    scale_led_power(psvr->state.leds[0]),
	    scale_led_power(psvr->state.leds[1]),
	    scale_led_power(psvr->state.leds[2]),
	    scale_led_power(psvr->state.leds[3]),
	    scale_led_power(psvr->state.leds[4]),
	    scale_led_power(psvr->state.leds[5]),
	    scale_led_power(psvr->state.leds[6]),
	    scale_led_power(psvr->state.leds[7]),
	    scale_led_power(psvr->state.leds[8]),
	    0,
	    0,
	    0,
	    0,
	    0,
	};

	return send_to_control(psvr, data, sizeof(data));
}

/*!
 * Control the leds on the headset, allowing you to turn on and off different
 * leds with a single call.
 *
 * @param[in] psvr   The PSVR to control leds on.
 * @param[in] adjust The leds to adjust with @p power.
 * @param[in] power  The power level to give to @p adjust leds.
 * @param[in] off    Leds that should be turned off,
 *                   @p adjust has higher priority.
 * @ingroup drv_psvr
 */
static int
control_leds(struct psvr_device *psvr, enum psvr_leds adjust, uint8_t power, enum psvr_leds off)
{
	// Get the leds we should control and remove any extra bits.
	enum psvr_leds all = (enum psvr_leds)((adjust | off) & PSVR_LED_ALL);
	if (all == 0) {
		// Nothing todo.
		return 0;
	}

	for (uint32_t i = 0; i < ARRAY_SIZE(psvr->wants.leds); i++) {
		uint32_t mask = (1 << i);
		if (adjust & mask) {
			psvr->wants.leds[i] = power;
		}
		if (off & mask) {
			psvr->wants.leds[i] = 0x00;
		}
	}

	return update_leds_if_changed(psvr);
}

static int
disco_leds(struct psvr_device *psvr)
{
	static const uint16_t leds[] = {
	    // First loop
	    PSVR_LED_A,
	    PSVR_LED_E,
	    PSVR_LED_B,
	    PSVR_LED_G,
	    PSVR_LED_D,
	    PSVR_LED_C,
	    PSVR_LED_F,
	    // Second loop
	    PSVR_LED_A,
	    PSVR_LED_E,
	    PSVR_LED_B,
	    PSVR_LED_G,
	    PSVR_LED_D,
	    PSVR_LED_C,
	    PSVR_LED_F,
	    // Blink loop
	    PSVR_LED_BACK,
	    PSVR_LED_FRONT,
	    PSVR_LED_BACK,
	    PSVR_LED_FRONT,
	    // All on after loop
	    PSVR_LED_ALL,
	};

	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		int ret = control_leds(psvr, (enum psvr_leds)leds[i], PSVR_LED_POWER_MAX, PSVR_LED_ALL);
		if (ret < 0) {
			return ret;
		}

		// Sleep for a tenth of a second while polling for packages.
		for (int k = 0; k < 100; k++) {
			ret = read_sensor_packets(psvr);
			if (ret < 0) {
				return ret;
			}

			ret = read_control_packets(psvr);
			if (ret < 0) {
				return ret;
			}

			os_nanosleep(1000 * 1000);
		}
	}

	return 0;
}

static void
teardown(struct psvr_device *psvr)
{
	// Stop the variable tracking.
	u_var_remove_root(psvr);

	// Includes null check, and sets to null.
	xrt_tracked_psvr_destroy(&psvr->tracker);

	if (psvr->hid_control != NULL) {
		// Turn off VR-mode and power down headset.
		if (control_vrmode_and_wait(psvr, false) < 0 || control_power_and_wait(psvr, false) < 0) {
			PSVR_ERROR(psvr, "Failed to shut down the headset!");
		}

		hid_close(psvr->hid_control);
		psvr->hid_control = NULL;
	}

	if (psvr->hid_sensor != NULL) {
		hid_close(psvr->hid_sensor);
		psvr->hid_sensor = NULL;
	}

	// Destroy the fusion.
	m_imu_3dof_close(&psvr->fusion);

	os_mutex_destroy(&psvr->device_mutex);
}


/*
 *
 * xrt_device functions.
 *
 */

static void
psvr_device_update_inputs(struct xrt_device *xdev)
{
	struct psvr_device *psvr = psvr_device(xdev);

	read_sensor_packets(psvr);
	update_leds_if_changed(psvr);
}

static void
psvr_device_get_tracked_pose(struct xrt_device *xdev,
                             enum xrt_input_name name,
                             uint64_t at_timestamp_ns,
                             struct xrt_space_relation *out_relation)
{
	struct psvr_device *psvr = psvr_device(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		PSVR_ERROR(psvr, "unknown input name");
		return;
	}

	os_mutex_lock(&psvr->device_mutex);

	// Read all packets.
	read_sensor_packets(psvr);
	read_control_packets(psvr);

	// Clear out the relation.
	U_ZERO(out_relation);

	// We have no tracking, don't return a position.
	if (psvr->tracker == NULL) {
		out_relation->pose.orientation = psvr->fusion.rot;

		out_relation->relation_flags = (enum xrt_space_relation_flags)(
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
	} else {
		xrt_tracked_psvr_get_tracked_pose(psvr->tracker, at_timestamp_ns, out_relation);
	}

	os_mutex_unlock(&psvr->device_mutex);

	//! @todo Move this to the tracker.
	// Make sure that the orientation is valid.
	math_quat_normalize(&out_relation->pose.orientation);
}

static void
psvr_device_get_view_pose(struct xrt_device *xdev,
                          struct xrt_vec3 *eye_relation,
                          uint32_t view_index,
                          struct xrt_pose *out_pose)
{
	struct xrt_pose pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
	bool adjust = view_index == 0;

	pose.position.x = eye_relation->x / 2.0f;
	pose.position.y = eye_relation->y / 2.0f;
	pose.position.z = eye_relation->z / 2.0f;

	// Adjust for left/right while also making sure there aren't any -0.f.
	if (pose.position.x > 0.0f && adjust) {
		pose.position.x = -pose.position.x;
	}
	if (pose.position.y > 0.0f && adjust) {
		pose.position.y = -pose.position.y;
	}
	if (pose.position.z > 0.0f && adjust) {
		pose.position.z = -pose.position.z;
	}

	*out_pose = pose;
}

static void
psvr_device_destroy(struct xrt_device *xdev)
{
	struct psvr_device *psvr = psvr_device(xdev);
	teardown(psvr);

	u_device_free(&psvr->base);
}

static bool
psvr_compute_distortion(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct psvr_device *psvr = psvr_device(xdev);

	return u_compute_distortion_panotools(&psvr->vals, u, v, result);
}


/*
 *
 * Exported functions.
 *
 */

struct xrt_device *
psvr_device_create(struct hid_device_info *sensor_hid_info,
                   struct hid_device_info *control_hid_info,
                   struct xrt_prober *xp,
                   enum u_logging_level log_level)
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct psvr_device *psvr = U_DEVICE_ALLOCATE(struct psvr_device, flags, 1, 0);
	int ret;

	psvr->log_level = log_level;
	psvr->base.update_inputs = psvr_device_update_inputs;
	psvr->base.get_tracked_pose = psvr_device_get_tracked_pose;
	psvr->base.get_view_pose = psvr_device_get_view_pose;
	psvr->base.compute_distortion = psvr_compute_distortion;
	psvr->base.destroy = psvr_device_destroy;
	psvr->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	psvr->base.name = XRT_DEVICE_GENERIC_HMD;

	{
		struct u_panotools_values vals = {0};

		vals.distortion_k[0] = 0.75;
		vals.distortion_k[1] = -0.01;
		vals.distortion_k[2] = 0.75;
		vals.distortion_k[3] = 0.0;
		vals.distortion_k[4] = 3.8;
		vals.aberration_k[0] = 0.999;
		vals.aberration_k[1] = 1.008;
		vals.aberration_k[2] = 1.018;
		vals.scale = 1.2 * (1980 / 2.0f);
		vals.viewport_size.x = (1980 / 2.0f);
		vals.viewport_size.y = (1080);
		vals.lens_center.x = vals.viewport_size.x / 2.0;
		vals.lens_center.y = vals.viewport_size.y / 2.0;

		psvr->vals = vals;

		struct xrt_hmd_parts *hmd = psvr->base.hmd;
		hmd->distortion.models = XRT_DISTORTION_MODEL_COMPUTE;
		hmd->distortion.preferred = XRT_DISTORTION_MODEL_COMPUTE;
	}

#if 1
	m_imu_3dof_init(&psvr->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);
#else
	psvr->fusion.rot.w = 1.0f;
#endif

	snprintf(psvr->base.str, XRT_DEVICE_NAME_LEN, "PS VR Headset");

	ret = open_hid(psvr, sensor_hid_info, &psvr->hid_sensor);
	if (ret != 0) {
		goto cleanup;
	}

	ret = open_hid(psvr, control_hid_info, &psvr->hid_control);
	if (ret < 0) {
		goto cleanup;
	}

	if (control_power_and_wait(psvr, true) < 0 || control_vrmode_and_wait(psvr, true) < 0) {
		goto cleanup;
	}

	// Device is now on and we can read calibration data now.
	ret = read_calibration_data(psvr);
	if (ret < 0) {
		goto cleanup;
	}

	if (debug_get_bool_option_psvr_disco()) {
		ret = disco_leds(psvr);
	} else {
		ret = control_leds(psvr, PSVR_LED_FRONT, PSVR_LED_POWER_MAX, (enum psvr_leds)0);
	}
	if (ret < 0) {
		PSVR_ERROR(psvr, "Failed to control leds '%i'", ret);
		goto cleanup;
	}


	/*
	 * Device setup.
	 */

	struct u_device_simple_info info;
	info.display.w_pixels = 1920;
	info.display.h_pixels = 1080;
	info.display.w_meters = 0.13f;
	info.display.h_meters = 0.07f;
	info.lens_horizontal_separation_meters = 0.13f / 2.0f;
	info.lens_vertical_position_meters = 0.07f / 2.0f;
	info.views[0].fov = 85.0f * (M_PI / 180.0f);
	info.views[1].fov = 85.0f * (M_PI / 180.0f);

	if (!u_device_setup_split_side_by_side(&psvr->base, &info)) {
		PSVR_ERROR(psvr, "Failed to setup basic device info");
		goto cleanup;
	}

	/*
	 * Setup variable.
	 */

	// clang-format off
	u_var_add_root(psvr, "PS VR Headset", true);
	u_var_add_gui_header(psvr, &psvr->gui.last_frame, "Last data");
	u_var_add_ro_vec3_i32(psvr, &psvr->last.samples[0].accel, "last.samples[0].accel");
	u_var_add_ro_vec3_i32(psvr, &psvr->last.samples[1].accel, "last.samples[1].accel");
	u_var_add_ro_vec3_i32(psvr, &psvr->last.samples[0].gyro, "last.samples[0].gyro");
	u_var_add_ro_vec3_i32(psvr, &psvr->last.samples[1].gyro, "last.samples[1].gyro");
	u_var_add_ro_vec3_f32(psvr, &psvr->read.accel, "read.accel");
	u_var_add_ro_vec3_f32(psvr, &psvr->read.gyro, "read.gyro");
	u_var_add_gui_header(psvr, &psvr->gui.control, "Control");
	u_var_add_u8(psvr, &psvr->wants.leds[0], "Led A");
	u_var_add_u8(psvr, &psvr->wants.leds[1], "Led B");
	u_var_add_u8(psvr, &psvr->wants.leds[2], "Led C");
	u_var_add_u8(psvr, &psvr->wants.leds[3], "Led D");
	u_var_add_u8(psvr, &psvr->wants.leds[4], "Led E");
	u_var_add_u8(psvr, &psvr->wants.leds[5], "Led F");
	u_var_add_u8(psvr, &psvr->wants.leds[6], "Led G");
	u_var_add_u8(psvr, &psvr->wants.leds[7], "Led H");
	u_var_add_u8(psvr, &psvr->wants.leds[8], "Led I");
	u_var_add_log_level(psvr, &psvr->log_level, "Log level");
	// clang-format on

	/*
	 * Finishing touches.
	 */

	if (psvr->log_level <= U_LOGGING_DEBUG) {
		u_device_dump_config(&psvr->base, __func__, "Sony PSVR");
	}

	// If there is a tracking factory use it.
	if (xp->tracking != NULL) {
		xp->tracking->create_tracked_psvr(xp->tracking, &psvr->base, &psvr->tracker);
	}

	// Use the new origin if we got a tracking system.
	if (psvr->tracker != NULL) {
		psvr->base.tracking_origin = psvr->tracker->origin;
	}

	psvr->base.orientation_tracking_supported = true;
	psvr->base.position_tracking_supported = xp->tracking != NULL;
	psvr->base.device_type = XRT_DEVICE_TYPE_HMD;

	PSVR_DEBUG(psvr, "YES!");

	os_mutex_init(&psvr->device_mutex);

	return &psvr->base;


cleanup:
	PSVR_DEBUG(psvr, "NO! :(");

	teardown(psvr);
	free(psvr);

	return NULL;
}
