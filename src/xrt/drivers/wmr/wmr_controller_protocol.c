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
                            struct wmr_controller_message *out_message,
                            enum u_logging_level log_level)
{
	if (len != 44) {
		U_LOG_IFL_E(log_level, "WMR Controller: unexpected message length: %zd", len);
		return false;
	}

	U_LOG_IFL_D(log_level,
	            "%02x %02x %02x %02x %02x %02x %02x %02x | "           // buttons and inputs, battery
	            "%02x %02x %02x %02x %02x %02x %02x %02x %02x | "      // accel
	            "%02x %02x | "                                         // temp
	            "%02x %02x %02x %02x %02x %02x %02x %02x %02x | "      // gyro
	            "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x | " // timestamp and more?
	            "%02x %02x %02x %02x %02x %02x",                       // device run state, status and more?
	            buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7], buffer[8],
	            buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15], buffer[16],
	            buffer[17], buffer[18], buffer[19], buffer[20], buffer[21], buffer[22], buffer[23], buffer[24],
	            buffer[25], buffer[26], buffer[27], buffer[28], buffer[29], buffer[30], buffer[31], buffer[32],
	            buffer[33], buffer[34], buffer[35], buffer[36], buffer[37], buffer[38], buffer[39], buffer[40],
	            buffer[41], buffer[42], buffer[43]);

	const unsigned char *p = buffer;

	out_message->buttons = read8(&p);

	// Todo: interpret analog stick data
	out_message->stick_1 = read8(&p);
	out_message->stick_2 = read8(&p);
	out_message->stick_3 = read8(&p);

	out_message->trigger = read8(&p); // pressure: 0x00 - 0xFF

	// Touchpad coords range: 0x00 - 0x64. Both are 0xFF when untouched.
	out_message->pad_x = read8(&p);
	out_message->pad_y = read8(&p);
	out_message->battery = read8(&p);
	out_message->accel_x = read24(&p);
	out_message->accel_y = read24(&p);
	out_message->accel_z = read24(&p);
	out_message->temp = read16(&p);
	out_message->gyro_x = read24(&p);
	out_message->gyro_y = read24(&p);
	out_message->gyro_z = read24(&p);

	out_message->timestamp = read32(&p); // Maybe only part of timestamp.
	read16(&p);                          // Unknown. Seems to depend on controller orientation.
	read32(&p);                          // Unknown.

	read16(&p); // Unknown. Device state, etc.
	read16(&p);
	read16(&p);

	U_LOG_IFL_D(log_level, "buttons: %02x, trigger: %02x, pad_x: %02x, pad_y: %02x", out_message->buttons,
	            out_message->trigger, out_message->pad_x, out_message->pad_y);

	return true;
}
