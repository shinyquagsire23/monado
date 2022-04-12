// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  WMR Motion Controller protocol helpers implementation.
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_logging.h"
#include "wmr_controller_protocol.h"

/*
 *
 * WMR Motion Controller protocol helpers
 *
 */

static inline void
vec3_from_wmr_controller_accel(const int32_t sample[3], struct xrt_vec3 *out_vec)
{
	// Reverb G1 observation: 1g is approximately 490,000.

	out_vec->x = (float)sample[0] / (98000 / 2);
	out_vec->y = (float)sample[1] / (98000 / 2);
	out_vec->z = (float)sample[2] / (98000 / 2);
}


static inline void
vec3_from_wmr_controller_gyro(const int32_t sample[3], struct xrt_vec3 *out_vec)
{
	out_vec->x = (float)sample[0] * 0.00001f;
	out_vec->y = (float)sample[1] * 0.00001f;
	out_vec->z = (float)sample[2] * 0.00001f;
}


bool
wmr_controller_packet_parse(const unsigned char *buffer,
                            size_t len,
                            struct wmr_controller_input *decoded_input,
                            enum u_logging_level log_level)
{
	if (len != 44) {
		U_LOG_IFL_E(log_level, "WMR Controller: unexpected message length: %zd", len);
		return false;
	}

	const unsigned char *p = buffer;

	// Read buttons
	uint8_t buttons = read8(&p);
	decoded_input->thumbstick.click = buttons & 0x01;
	decoded_input->home = buttons & 0x02;
	decoded_input->menu = buttons & 0x04;
	decoded_input->squeeze = buttons & 0x08; // squeeze-click
	decoded_input->trackpad.click = buttons & 0x10;
	decoded_input->bt_pairing = buttons & 0x20;
	decoded_input->trackpad.touch = buttons & 0x40;


	// Read thumbstick coordinates (12 bit resolution)
	int16_t stick_x = read8(&p);
	uint8_t nibbles = read8(&p);
	stick_x += ((nibbles & 0x0F) << 8);
	int16_t stick_y = (nibbles >> 4);
	stick_y += (read8(&p) << 4);

	decoded_input->thumbstick.values.x = (float)(stick_x - 0x07FF) / 0x07FF;
	if (decoded_input->thumbstick.values.x > 1.0f) {
		decoded_input->thumbstick.values.x = 1.0f;
	}

	decoded_input->thumbstick.values.y = (float)(stick_y - 0x07FF) / 0x07FF;
	if (decoded_input->thumbstick.values.y > 1.0f) {
		decoded_input->thumbstick.values.y = 1.0f;
	}

	// Read trigger value (0x00 - 0xFF)
	decoded_input->trigger = (float)read8(&p) / 0xFF;

	// Read trackpad coordinates (0x00 - 0x64. Both are 0xFF when untouched)
	uint8_t trackpad_x = read8(&p);
	uint8_t trackpad_y = read8(&p);
	decoded_input->trackpad.values.x = (trackpad_x == 0xFF) ? 0.0f : (float)(trackpad_x - 0x32) / 0x32;
	decoded_input->trackpad.values.y = (trackpad_y == 0xFF) ? 0.0f : (float)(trackpad_y - 0x32) / 0x32;


	decoded_input->battery = read8(&p);


	int32_t acc[3];
	acc[0] = read24(&p); // x
	acc[1] = read24(&p); // y
	acc[2] = read24(&p); // z
	vec3_from_wmr_controller_accel(acc, &decoded_input->imu.acc);

	U_LOG_IFL_T(log_level, "Accel [m/s^2] : %f",
	            sqrtf(decoded_input->imu.acc.x * decoded_input->imu.acc.x +
	                  decoded_input->imu.acc.y * decoded_input->imu.acc.y +
	                  decoded_input->imu.acc.z * decoded_input->imu.acc.z));


	decoded_input->imu.temperature = read16(&p);


	int32_t gyro[3];
	gyro[0] = read24(&p);
	gyro[1] = read24(&p);
	gyro[2] = read24(&p);
	vec3_from_wmr_controller_gyro(gyro, &decoded_input->imu.gyro);


	uint32_t prev_ticks = decoded_input->imu.timestamp_ticks & 0xFFFFFFFFUL;

	// Write the new ticks value into the lower half of timestamp_ticks
	decoded_input->imu.timestamp_ticks &= (0xFFFFFFFFUL << 32);
	decoded_input->imu.timestamp_ticks += (uint32_t)read32(&p);

	if ((decoded_input->imu.timestamp_ticks & 0xFFFFFFFFUL) < prev_ticks) {
		// Timer overflow, so increment the upper half of timestamp_ticks
		decoded_input->imu.timestamp_ticks += (0x1UL << 32);
	}

	/* Todo: More decoding here
	    read16(&p); // Unknown. Seems to depend on controller orientation.
	    read32(&p); // Unknown.
	    read16(&p); // Unknown. Device state, etc.
	    read16(&p);
	    read16(&p);
	*/

	return true;
}
