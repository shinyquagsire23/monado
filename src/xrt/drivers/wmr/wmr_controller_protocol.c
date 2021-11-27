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

	/*
	        U_LOG_IFL_D(log_level,
	                    "%02x %02x %02x %02x %02x %02x %02x %02x | "           // buttons and inputs, battery
	                    "%02x %02x %02x %02x %02x %02x %02x %02x %02x | "      // accel
	                    "%02x %02x | "                                         // temp
	                    "%02x %02x %02x %02x %02x %02x %02x %02x %02x | "      // gyro
	                    "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x | " // timestamp and more?
	                    "%02x %02x %02x %02x %02x %02x",                       // device run state, status and more?
	                    buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7],
	   buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15], buffer[16],
	                    buffer[17], buffer[18], buffer[19], buffer[20], buffer[21], buffer[22], buffer[23],
	   buffer[24], buffer[25], buffer[26], buffer[27], buffer[28], buffer[29], buffer[30], buffer[31], buffer[32],
	                    buffer[33], buffer[34], buffer[35], buffer[36], buffer[37], buffer[38], buffer[39],
	   buffer[40], buffer[41], buffer[42], buffer[43]);
	*/
	const unsigned char *p = buffer;

	// Read buttons
	unsigned char buttons = read8(&p);
	decoded_input->thumbstick.click = buttons & 0x01;
	// decoded_input->home = buttons & 0x02;
	decoded_input->menu = buttons & 0x04;
	decoded_input->squeeze = buttons & 0x08; // squeeze-click
	decoded_input->trackpad.click = buttons & 0x10;
	// decoded_input->bt_pairing = buttons & 0x20;
	decoded_input->trackpad.touch = buttons & 0x40;


	// Read thumbstick coordinates (12 bit resolution)
	signed int stick_x = read8(&p);
	unsigned char nipples = read8(&p);
	stick_x += ((nipples & 0x0F) << 8);
	signed int stick_y = (nipples >> 4);
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

	U_LOG_IFL_D(log_level, "thumbstick: x %f, y %f, trigger: %f", decoded_input->thumbstick.values.x,
	            decoded_input->thumbstick.values.y, decoded_input->trigger);


	// Read trackpad coordinates (0x00 - 0x64. Both are 0xFF when untouched)
	unsigned char trackpad_x = read8(&p);
	unsigned char trackpad_y = read8(&p);
	decoded_input->trackpad.values.x = (trackpad_x == 0xFF) ? 0.0f : (float)(trackpad_x - 0x32) / 0x32;
	decoded_input->trackpad.values.y = (trackpad_y == 0xFF) ? 0.0f : (float)(trackpad_y - 0x32) / 0x32;

	U_LOG_IFL_D(log_level, "touchpad: x %f, y %f", decoded_input->trackpad.values.x,
	            decoded_input->trackpad.values.y);


	/* Todo: More decoding here

	    unsigned char battery = read8(&p);
	    unsigned int accel_x = read24(&p);
	    unsigned int accel_y = read24(&p);
	    unsigned int accel_z = read24(&p);
	    unsigned int temp = read16(&p);
	    unsigned int gyro_x = read24(&p);
	    unsigned int gyro_y = read24(&p);
	    unsigned int gyro_z = read24(&p);

	    unsigned int timestamp = read32(&p); // Maybe only part of timestamp.
	    read16(&p);                          // Unknown. Seems to depend on controller orientation.
	    read32(&p);                          // Unknown.

	    read16(&p); // Unknown. Device state, etc.
	    read16(&p);
	    read16(&p);
	*/

	return true;
}
