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


struct wmr_controller_input
{
	bool menu;
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
                            struct wmr_controller_input *decoded_input,
                            enum u_logging_level log_level);


/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
