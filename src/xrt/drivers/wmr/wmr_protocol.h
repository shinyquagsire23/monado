// Copyright 2018, Philipp Zabel.
// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  WMR and MS HoloLens protocol constants, structures and helpers header
 * @author Philipp Zabel <philipp.zabel@gmail.com>
 * @author nima01 <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */

#pragma once

#include "math/m_vec2.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * WMR and MS HoloLens Sensors protocol constants and structures
 *
 * @addtogroup drv_wmr
 * @{
 */

#define WMR_FEATURE_BUFFER_SIZE 497
#define WMR_MS_HOLOLENS_NS_PER_TICK 100

// Messages types specific to WMR Hololens Sensors devices
#define WMR_MS_HOLOLENS_MSG_SENSORS 0x01
#define WMR_MS_HOLOLENS_MSG_CONTROL 0x02 // Firmware read control responses
#define WMR_MS_HOLOLENS_MSG_DEBUG 0x03
#define WMR_MS_HOLOLENS_MSG_BT_IFACE 0x05         /* Bluetooth interface */
#define WMR_MS_HOLOLENS_MSG_LEFT_CONTROLLER 0x06  /* Left controller */
#define WMR_MS_HOLOLENS_MSG_RIGHT_CONTROLLER 0x0E /* Right controller */
#define WMR_MS_HOLOLENS_MSG_BT_CONTROL 0x16       /* BT control message on Reverb G2 & Odyssey+ */
#define WMR_MS_HOLOLENS_MSG_CONTROLLER_STATUS 0x17

// Messages types specific to WMR Hololens Sensors' companion devices
#define WMR_CONTROL_MSG_IPD_VALUE 0x01
#define WMR_CONTROL_MSG_UNKNOWN_02 0x02    // Seen in relation to proximity events on Reverb G1
#define WMR_CONTROL_MSG_DEVICE_STATUS 0x05 // Seen in relation screen state changes on Reverb G1

// Message sub-types for WMR_MS_HOLOLENS_MSG_BT_IFACE WMR Hololens Sensors message
#define WMR_BT_IFACE_MSG_DEBUG 0x19

// Controller status codes for WMR_MS_HOLOLENS_MSG_CONTROLLER_STATUS status message
#define WMR_CONTROLLER_STATUS_UNPAIRED 0x0
#define WMR_CONTROLLER_STATUS_OFFLINE 0x1
#define WMR_CONTROLLER_STATUS_ONLINE 0x2

/* Messages we can send the G2 via WMR_MS_HOLOLENS_MSG_BT_CONTROL */
enum wmr_bt_control_msg
{
	WMR_BT_CONTROL_MSG_ONLINE_STATUS = 0x04,
	WMR_BT_CONTROL_MSG_PAIR = 0x05,
	WMR_BT_CONTROL_MSG_UNPAIR = 0x06,
	WMR_BT_CONTROL_MSG_PAIRING_STATUS = 0x08,
	WMR_BT_CONTROL_MSG_CMD_STATUS = 0x09,
};

#define STR_TO_U32(s) ((uint32_t)(((s)[0]) | ((s)[1] << 8) | ((s)[2] << 16) | ((s)[3] << 24)))
#define WMR_MAGIC STR_TO_U32("Dlo+")

#define WMR_MIN_EXPOSURE 60
#define WMR_MAX_OBSERVED_EXPOSURE 6000
#define WMR_MAX_EXPOSURE 9000
#define WMR_MIN_GAIN 16
#define WMR_MAX_GAIN 255

static const unsigned char hololens_sensors_imu_on[64] = {0x02, 0x07};


struct hololens_sensors_packet
{
	uint8_t id;
	uint16_t temperature[4];
	uint64_t gyro_timestamp[4];
	int16_t gyro[3][4 * 8];
	uint64_t accel_timestamp[4];
	int32_t accel[3][4];
	uint64_t video_timestamp[4];
};

struct wmr_config_header
{
	uint32_t json_start;
	uint32_t json_size;
	char manufacturer[0x40];
	char device[0x40];
	char serial[0x40];
	char uid[0x26];
	char unk[0xd5];
	char name[0x40];
	char revision[0x20];
	char revision_date[0x20];
};

/*!
 * @}
 */


/*!
 * WMR and MS HoloLens Sensors protocol helpers
 *
 * @addtogroup drv_wmr
 * @{
 */

void
vec3_from_hololens_accel(int32_t sample[3][4], int i, struct xrt_vec3 *out_vec);

void
vec3_from_hololens_gyro(int16_t sample[3][32], int i, struct xrt_vec3 *out_vec);


static inline uint8_t
read8(const unsigned char **buffer)
{
	uint8_t ret = **buffer;
	*buffer += 1;
	return ret;
}

static inline int16_t
read16(const unsigned char **buffer)
{
	int16_t ret = (*(*buffer + 0) << 0) | //
	              (*(*buffer + 1) << 8);
	*buffer += 2;
	return ret;
}

static inline int32_t
read24(const unsigned char **buffer)
{
	// Note: Preserve sign by shifting up to write MSB
	int32_t ret = (*(*buffer + 0) << 8) | (*(*buffer + 1) << 16) | (*(*buffer + 2) << 24);
	*buffer += 3;

	// restore 24 bit scale again
	return ret >> 8;
}

static inline int32_t
read32(const unsigned char **buffer)
{
	int32_t ret = (*(*buffer + 0) << 0) |  //
	              (*(*buffer + 1) << 8) |  //
	              (*(*buffer + 2) << 16) | //
	              (*(*buffer + 3) << 24);
	*buffer += 4;
	return ret;
}

static inline uint64_t
read64(const unsigned char **buffer)
{
	uint64_t ret = ((uint64_t) * (*buffer + 0) << 0) |  //
	               ((uint64_t) * (*buffer + 1) << 8) |  //
	               ((uint64_t) * (*buffer + 2) << 16) | //
	               ((uint64_t) * (*buffer + 3) << 24) | //
	               ((uint64_t) * (*buffer + 4) << 32) | //
	               ((uint64_t) * (*buffer + 5) << 40) | //
	               ((uint64_t) * (*buffer + 6) << 48) | //
	               ((uint64_t) * (*buffer + 7) << 56);
	*buffer += 8;
	return ret;
}

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
