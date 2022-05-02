/*
 * Copyright 2019 Lucas Teske <lucas@teske.com.br>
 * Copyright 2019-2020 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 * OpenHMD - Free and Open Source API and drivers for immersive technology.
 */

/*!
 * @file
 * @brief  Oculus Rift S USB protocol implementation interface.
 *
 * Functions for interpreting the USB protocol to the
 * headset and Touch Controllers (via the headset's radio link)
 *
 * Ported from OpenHMD
 *
 * @author Jan Schmidt <jan@centricular.com>
 */
#ifndef __RIFT_S_PROTOCOL__
#define __RIFT_S_PROTOCOL__

#include <stdlib.h>
#include "os/os_hid.h"
#include "xrt/xrt_defines.h"

#define FEATURE_BUFFER_SIZE 256

#define KEEPALIVE_INTERVAL_MS 1000
#define CAMERA_REPORT_INTERVAL_MS 1000

#define RIFT_S_BUTTON_A_X 0x01
#define RIFT_S_BUTTON_B_Y 0x02
#define RIFT_S_BUTTON_STICK 0x04
#define RIFT_S_BUTTON_MENU_OCULUS 0x08

#define RIFT_S_BUTTON_UNKNOWN 0x10 // Unknown mask value seen sometimes. Low battery?

#define RIFT_S_FINGER_A_X_STRONG 0x01
#define RIFT_S_FINGER_B_Y_STRONG 0x02
#define RIFT_S_FINGER_STICK_STRONG 0x04
#define RIFT_S_FINGER_TRIGGER_STRONG 0x08
#define RIFT_S_FINGER_A_X_WEAK 0x10
#define RIFT_S_FINGER_B_Y_WEAK 0x20
#define RIFT_S_FINGER_STICK_WEAK 0x40
#define RIFT_S_FINGER_TRIGGER_WEAK 0x80

typedef enum
{
	RIFT_S_CTRL_MASK08 = 0x08,   /* Unknown. Vals seen 0x28, 0x0a, 0x32, 0x46, 0x00... */
	RIFT_S_CTRL_BUTTONS = 0x0c,  /* Button states */
	RIFT_S_CTRL_FINGERS = 0x0d,  /* Finger positions */
	RIFT_S_CTRL_MASK0e = 0x0e,   /* Unknown. Only seen 0x00 */
	RIFT_S_CTRL_TRIGGRIP = 0x1b, /* Trigger + Grip */
	RIFT_S_CTRL_JOYSTICK = 0x22, /* Joystick X/Y */
	RIFT_S_CTRL_CAPSENSE = 0x27, /* Capsense */
	RIFT_S_CTRL_IMU = 0x91
} rift_s_controller_block_id_t;

typedef enum
{
	RIFT_S_DEVICE_TYPE_UNKNOWN = 0,
	RIFT_S_DEVICE_LEFT_CONTROLLER = 0x13001101,
	RIFT_S_DEVICE_RIGHT_CONTROLLER = 0x13011101,
} rift_s_device_type;

#pragma pack(push, 1)
typedef struct
{
	uint8_t id;

	uint32_t timestamp;
	uint16_t unknown_varying2;

	int16_t accel[3];
	int16_t gyro[3];
} rift_s_controller_imu_block_t;

typedef struct
{
	/* 0x08, 0x0c, 0x0d or 0x0e block */
	uint8_t id;

	uint8_t val;
} rift_s_controller_maskbyte_block_t;

typedef struct
{
	/* 0x1b trigger/grip block */
	uint8_t id;
	uint8_t vals[3];
} rift_s_controller_triggrip_block_t;

typedef struct
{
	/* 0x22 joystick axes block */
	uint8_t id;
	uint32_t val;
} rift_s_controller_joystick_block_t;

typedef struct
{
	/* 0x27 - capsense block */
	uint8_t id;

	uint8_t a_x;
	uint8_t b_y;
	uint8_t joystick;
	uint8_t trigger;
} rift_s_controller_capsense_block_t;

typedef struct
{
	uint8_t data[19];
} rift_s_controller_raw_block_t;

typedef union {
	uint8_t block_id;
	rift_s_controller_imu_block_t imu;
	rift_s_controller_maskbyte_block_t maskbyte;
	rift_s_controller_triggrip_block_t triggrip;
	rift_s_controller_joystick_block_t joystick;
	rift_s_controller_capsense_block_t capsense;
	rift_s_controller_raw_block_t raw;
} rift_s_controller_info_block_t;
#pragma pack(pop)

typedef struct
{
	uint8_t id;

	uint64_t device_id;

	/* Length of the data block, which contains variable length entries
	 * If this is < 4, then the flags and log aren't valid. */
	uint8_t data_len;

	/* 0x04 = new log line
	 * 0x02 = parity bit, toggles each line when receiving log chars
	 * other bits, unknown */
	uint8_t flags;
	// Contains up to 3 bytes of debug log chars
	uint8_t log[3];

	uint8_t num_info;
	rift_s_controller_info_block_t info[8];

	uint8_t extra_bytes_len;
	uint8_t extra_bytes[48];
} rift_s_controller_report_t;

#pragma pack(push, 1)
typedef struct
{
	uint8_t marker;

	int16_t accel[3];
	int16_t gyro[3];
	int16_t temperature;
} rift_s_hmd_imu_sample_t;

typedef struct
{
	uint8_t id;
	uint16_t unknown_const1;

	uint32_t timestamp;

	rift_s_hmd_imu_sample_t samples[3];

	uint8_t marker;
	uint8_t unknown2;

	/* Frame timestamp and ID increment when the screen is running,
	 * every 12.5 ms (80Hz) */
	uint32_t frame_timestamp;
	int16_t unknown_zero1;
	int16_t frame_id;
	int16_t unknown_zero2;
} rift_s_hmd_report_t;

/* Read/Write using report 5 */
/*
 *    05 O1 O2 P1 P1 P2 P2 P3 P3 P4 P4 P5 P5 E1 E1 E3
 *    E4 E5 U1 U2 U3 A1 A1 A1 A1 A2 A2 A2 A2 A3 A3 A3
 *    A3 A4 A4 A4 A4 A5 A5 A5 A5
 *
 *    O1 = Camera stream on (0x00 = off, 0x1 = on)
 *    O2 = Radio Sync? (Usage not clear, but seems to sometimes affect sync)
 *    Px = Exposure *and* Vertical offset / position of camera x passthrough view
 *         Seems to take values from 0x1db7-0x36b3. Values above 0x36c6 are ignored.
 *    Ex = Gain of camera x passthrough view
 *    U1U2U3 = 26 00 40 always?
 *    Ax = ? of camera x. 4 byte LE, Always seems to take values 0x3b0-0x4ff
 *         but I can't see the effect on the images, either controller or passthrough
 */
typedef struct
{
	uint8_t id;
	uint8_t uvc_enable;
	uint8_t radio_sync_flag;
	/* One slot per camera: */
	uint16_t slam_frame_exposures[5];
	uint8_t slam_frame_gains[5];

	uint8_t marker[3]; // 0x26 0x00 0x40

	uint32_t unknown32[5];
} rift_s_camera_report_t;

/* Read using report 6 */
typedef struct
{
	uint8_t cmd;
	uint16_t v_resolution;
	uint16_t h_resolution;
	uint16_t unknown1;
	uint8_t refresh_rate;
	uint8_t unknown2[14];
} rift_s_panel_info_t;

/* Read using report 9 */
struct rift_s_imu_config_info_t
{
	uint8_t cmd;
	uint32_t imu_hz;
	float gyro_scale;        /* Gyro = reading / gyro_scale - in degrees */
	float accel_scale;       /* Accel = reading * g / accel_scale */
	float temperature_scale; /* Temperature = reading / scale + offset */
	float temperature_offset;
};

/* Packet read from endpoint 11 (0x0b) */
typedef struct
{
	uint8_t cmd;
	uint8_t seqnum;
	uint8_t busy_flag;
	uint8_t response_bytes[197];
} rift_s_hmd_radio_response_t;

/* Struct for sending radio commands to 0x12 / 0x13 */
typedef struct
{
	uint8_t cmd;
	uint64_t device_id;
	uint8_t cmd_bytes[52];
} rift_s_hmd_radio_command_t;

typedef struct
{
	uint64_t device_id;
	uint32_t device_type;
	uint64_t empty[2];
} rift_s_device_type_record_t;
#pragma pack(pop)

/* The maximum number that can fit in a 200 byte report */
#define DEVICES_LIST_MAX_DEVICES 7

typedef struct
{
	uint8_t num_devices;

	rift_s_device_type_record_t devices[DEVICES_LIST_MAX_DEVICES];
} rift_s_devices_list_t;

int
rift_s_read_firmware_version(struct os_hid_device *hid);
int
rift_s_read_panel_info(struct os_hid_device *hid, rift_s_panel_info_t *panel_info);
int
rift_s_read_imu_config_info(struct os_hid_device *hid, struct rift_s_imu_config_info_t *imu_config);
int
rift_s_read_fw_proximity_threshold(struct os_hid_device *hid, int *proximity_threshold);
int
rift_s_protocol_set_proximity_threshold(struct os_hid_device *hid, uint16_t threshold);
int
rift_s_hmd_enable(struct os_hid_device *hid, bool enable);
int
rift_s_set_screen_enable(struct os_hid_device *hid, bool enable);

void
rift_s_send_keepalive(struct os_hid_device *hid);

void
rift_s_protocol_camera_report_init(rift_s_camera_report_t *camera_report);
int
rift_s_protocol_send_camera_report(struct os_hid_device *hid, rift_s_camera_report_t *camera_report);

bool
rift_s_parse_hmd_report(rift_s_hmd_report_t *report, const unsigned char *buf, int size);
bool
rift_s_parse_controller_report(rift_s_controller_report_t *report, const unsigned char *buf, int size);
int
rift_s_read_firmware_block(struct os_hid_device *handle, uint8_t block_id, char **data_out, int *len_out);

int
rift_s_read_devices_list(struct os_hid_device *handle, rift_s_devices_list_t *dev_list);

void
rift_s_hexdump_buffer(const char *label, const unsigned char *buf, int length); // Debugging
int
rift_s_snprintf_hexdump_buffer(
    char *outbuf, size_t outbufsize, const char *label, const unsigned char *buf, int length); // Debugging
#endif
