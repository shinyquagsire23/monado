// Copyright 2016, Joey Ferwerda.
// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PSVR device header, imported from OpenHMD.
 * @author Joey Ferwerda <joeyferweda@gmail.com>
 * @author Philipp Zabel <philipp.zabel@gmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_psvr
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_prober.h"

#include "util/u_logging.h"

#include <hidapi.h>


#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Defines
 *
 */

#define PSVR_VID 0x054c
#define PSVR_PID 0x09af

#define PSVR_HANDLE_IFACE 4
#define PSVR_CONTROL_IFACE 5

enum psvr_status_bits
{
	// clang-format off
	PSVR_STATUS_BIT_POWER                = (1 << 0),
	PSVR_STATUS_BIT_HMD_WORN             = (1 << 1),
	PSVR_STATUS_BIT_CINEMATIC_MODE       = (1 << 2),
	PSVR_STATUS_BIT_UNKNOWN_BIT_3        = (1 << 3),
	PSVR_STATUS_BIT_HEADPHONES_CONNECTED = (1 << 4),
	PSVR_STATUS_BIT_MUTE_ENABLED         = (1 << 5),
	PSVR_STATUS_BIT_UNKNOWN_BIT_6        = (1 << 6),
	PSVR_STATUS_BIT_UNKNOWN_BIT_7        = (1 << 7),
	// clang-format on
};

#define PSVR_STATUS_VR_MODE_OFF 0
#define PSVR_STATUS_VR_MODE_ON 1

#define PSVR_TICKS_PER_SECOND (1000000.0) // 1 MHz ticks
#define PSVR_NS_PER_TICK (1000)           // Each tick is a microsecond

#define PSVR_PKG_STATUS 0xF0
#define PSVR_PKG_DEVICE_NAME 0x80
#define PSVR_PKG_CALIBRATION 0x86
#define PSVR_PKG_0xA0 0xA0
#define PSVR_PKG_0x82 0x82

#define PSVR_GET_DATA_ID_DEVICE_NAME 0x80
#define PSVR_GET_DATA_ID_CALIBRATION 0x86
#define PSVR_GET_DATA_ID_0x82 0x82


/*
 *
 * Structs
 *
 */

/*!
 * A parsed single gyro, accel and tick sample.
 *
 * @ingroup drv_psvr
 */
struct psvr_parsed_sample
{
	struct xrt_vec3_i32 accel;
	struct xrt_vec3_i32 gyro;
	uint32_t tick;
};

/*!
 * Over the wire sensor packet from the headset.
 *
 * @ingroup drv_psvr
 */
struct psvr_parsed_sensor
{
	uint8_t buttons;
	uint8_t state;
	uint16_t volume;
	uint16_t button_raw;
	uint16_t proximity;
	uint8_t seq;

	struct psvr_parsed_sample samples[2];
};

/*!
 * A status packet from the headset in wire format.
 *
 * @ingroup drv_psvr
 */
struct psvr_parsed_status
{
	uint8_t status;
	uint8_t volume;
	uint8_t display_time;
	uint8_t vr_mode;
};


/*
 *
 * Functions
 *
 */

struct xrt_device *
psvr_device_create(struct hid_device_info *sensor_hid_info,
                   struct hid_device_info *control_hid_info,
                   struct xrt_prober *xp,
                   enum u_logging_level log_level);

bool
psvr_parse_sensor_packet(struct psvr_parsed_sensor *sensor, const uint8_t *buffer, int size);

bool
psvr_parse_status_packet(struct psvr_parsed_status *status, const uint8_t *buffer, int size);


#ifdef __cplusplus
}
#endif
