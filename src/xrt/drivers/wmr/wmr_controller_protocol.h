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
 * @addtogroup drv_wmr
 * @{
 */

// Todo: Is this enough?
#define WMR_MOTION_CONTROLLER_MSG_BUFFER_SIZE 256
#define WMR_MOTION_CONTROLLER_NS_PER_TICK 100


// Messages types specific to Bluetooth connected WMR motion controllers
#define WMR_BT_MOTION_CONTROLLER_MSG 0x01


struct wmr_controller_input
{
	// buttons clicked
	bool menu;
	bool home;
	bool bt_pairing;
	bool squeeze; // Actually a "squeeze" click

	float trigger;

	struct
	{
		bool click;
		struct xrt_vec2 values;
	} thumbstick;
	struct
	{
		bool click;
		bool touch;
		struct xrt_vec2 values;
	} trackpad;

	uint8_t battery;

	struct
	{
		uint64_t timestamp_ticks;
		struct xrt_vec3 acc;
		struct xrt_vec3 gyro;
		int32_t temperature;
	} imu;
};

/*!
 * @}
 */


/*!
 * WMR Motion Controller protocol helpers
 *
 * @addtogroup drv_wmr
 * @{
 */

bool
wmr_controller_packet_parse(const unsigned char *buffer,
                            size_t len,
                            struct wmr_controller_input *decoded_input,
                            enum u_logging_level log_level);


/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
