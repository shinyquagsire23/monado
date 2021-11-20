// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  WMR Motion Controller protocol constants, structures and helpers
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */

#pragma once

#include "wmr_protocol.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * WMR Motion Controller protocol constant and structures
 *
 * @ingroup drv_wmr
 * @{
 */

// Todo: Is this enough?
#define WMR_MOTION_CONTROLLER_MSG_BUFFER_SIZE 256


// Messages types specific to Bluetooth connected WMR motion controllers
#define WMR_BT_MOTION_CONTROLLER_MSG 0x01


struct wmr_controller_message
{
	// Very much still work in progress!

	// HP Reverb G1 button map:
	// Stick_pressed: 0x01
	// Home button pressed: 0x02
	// Menu button pressed: 0x04
	// Grip button pressed: 0x08
	// Touch-pad pressed: 0x10
	// BT pairing button pressed: 0x20
	// Touch-pad touched: 0x40
	uint8_t buttons;

	// Todo: interpret analog stick data
	uint8_t stick_1;
	uint8_t stick_2;
	uint8_t stick_3;

	uint8_t trigger; // pressure: 0x00 - 0xFF

	// Touchpad coords range: 0x00 - 0x64. Both are 0xFF when untouched.
	uint8_t pad_x;
	uint8_t pad_y;

	uint8_t battery;

	int32_t accel_x;
	int32_t accel_y;
	int32_t accel_z;

	int32_t temp;

	int32_t gyro_x;
	int32_t gyro_y;
	int32_t gyro_z;

	uint64_t timestamp;
};


/*!
 * @}
 */


/*!
 * WMR Motion Controller protocol helpers
 *
 * @ingroup drv_wmr
 * @{
 */

bool
wmr_controller_packet_parse(const unsigned char *buffer,
                            size_t len,
                            struct wmr_controller_message *out_message,
                            enum u_logging_level log_level);


/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
