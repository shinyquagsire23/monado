// Copyright 2016, Joey Ferwerda.
// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PSVR packet parsing implementation, imported from OpenHMD.
 * @author Joey Ferwerda <joeyferweda@gmail.com>
 * @author Philipp Zabel <philipp.zabel@gmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_psvr
 */

#include "xrt/xrt_compiler.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "psvr_device.h"

#include <stdio.h>


/*
 *
 * Helper functions
 *
 */

inline static void
skip(const uint8_t **buffer, size_t num)
{
	*buffer += num;
}

inline static void
read_u8(const uint8_t **buffer, uint8_t *out_value)
{
	*out_value = **buffer;
	*buffer += 1;
}

inline static void
read_u16(const uint8_t **buffer, uint16_t *out_value)
{
	*out_value = (*(*buffer + 0) << 0) | // Byte 0
	             (*(*buffer + 1) << 8);  // Byte 1
	*buffer += 2;
}

inline static void
read_i16(const uint8_t **buffer, int16_t *out_value)
{
	*out_value = (*(*buffer + 0) << 0) | // Byte 0
	             (*(*buffer + 1) << 8);  // Byte 1
	*buffer += 2;
}

inline static void
read_u32(const uint8_t **buffer, uint32_t *out_value)
{
	*out_value = (*(*buffer + 0) << 0) |  // Byte 0
	             (*(*buffer + 1) << 8) |  // Byte 1
	             (*(*buffer + 2) << 16) | // Byte 2
	             (*(*buffer + 3) << 24);  // Byte 3
	*buffer += 4;
}

static void
read_sensor(const uint8_t **buffer, struct psvr_sensor_sample *sample)
{
	// Tick.
	read_u32(buffer, &sample->tick);

	// Rotation.
	read_i16(buffer, &sample->gyro[0]);
	read_i16(buffer, &sample->gyro[1]);
	read_i16(buffer, &sample->gyro[2]);

	// Acceleration.
	read_i16(buffer, &sample->accel[0]);
	read_i16(buffer, &sample->accel[1]);
	read_i16(buffer, &sample->accel[2]);
}


/*
 *
 * Exported functions
 *
 */

bool
psvr_parse_sensor_packet(struct psvr_sensor_packet *packet,
                         const uint8_t *buffer,
                         int size)
{
	const uint8_t *start = buffer;

	if (size != 64) {
		return false;
	}

	// Buttons.
	read_u8(&buffer, &packet->buttons);

	// Unknown, skip 1 bytes.
	skip(&buffer, 1);

	// Volume.
	read_u16(&buffer, &packet->volume);

	// Unknown, skip 1 bytes.
	skip(&buffer, 1);

	// State.
	read_u8(&buffer, &packet->state);

	// Unknown, skip 10 bytes.
	skip(&buffer, 10);

	// Two sensors.
	read_sensor(&buffer, &packet->samples[0]);
	read_sensor(&buffer, &packet->samples[1]);

	// unknown, skip 5 bytes.
	skip(&buffer, 5);

	// Raw button data.
	read_u16(&buffer, &packet->button_raw);

	// Proximity, ~150 (nothing) to 1023 (headset is on).
	read_u16(&buffer, &packet->proximity);

	// Unknown, skip 6 bytes.
	skip(&buffer, 6);

	// Finally a sequence number.
	read_u8(&buffer, &packet->seq);

	return (size_t)buffer - (size_t)start == 64;
}

bool
psvr_parse_status_packet(struct psvr_status_packet *packet,
                         const uint8_t *buffer,
                         int size)
{
	const uint8_t *start = buffer;

	if (size != 20) {
		return false;
	}

	// Header.
	skip(&buffer, 4);

	// Status bits.
	read_u8(&buffer, &packet->status);

	// Volume.
	read_u8(&buffer, &packet->volume);

	// Unknown, 0x00, 0x00.
	skip(&buffer, 2);

	// Display time in minutes.
	read_u8(&buffer, &packet->display_time);

	// Unknown, 0xFF, 0x00.
	skip(&buffer, 2);

	// VR Mode Active.
	read_u8(&buffer, &packet->vr_mode);

	// Unknown, 0x12, 0x00...
	skip(&buffer, 8);

	return (size_t)buffer - (size_t)start == 20;
}
