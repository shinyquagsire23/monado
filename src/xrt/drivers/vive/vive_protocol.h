// Copyright 2016-2019, Philipp Zabel
// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vive USB HID reports
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include <asm/byteorder.h>
#include <stdint.h>
#include "os/os_hid.h"

#define VIVE_CONTROLLER_BUTTON_REPORT_ID 0x01

#define VIVE_CONTROLLER_USB_BUTTON_TRIGGER (1 << 0)
#define VIVE_CONTROLLER_USB_BUTTON_GRIP (1 << 2)
#define VIVE_CONTROLLER_USB_BUTTON_MENU (1 << 12)
#define VIVE_CONTROLLER_USB_BUTTON_SYSTEM (1 << 13)
#define VIVE_CONTROLLER_USB_BUTTON_THUMB (1 << 18)
#define VIVE_CONTROLLER_USB_BUTTON_TOUCH (1 << 20)

struct vive_controller_button_report
{
	uint8_t id;
	uint8_t unknown1;
	uint16_t maybe_type;
	uint32_t sequence;
	uint32_t buttons;
	union {
		uint16_t trigger;
		uint16_t battery_voltage;
	};
	uint8_t battery;
	uint8_t unknown2;
	uint32_t hardware_id;
	uint16_t touch[2];
	uint16_t unknown3;
	uint16_t trigger_hires;
	uint8_t unknown4[24];
	uint16_t trigger_raw;
	uint8_t unknown5[8];
	uint8_t maybe_bitfield;
	uint8_t unknown6;
} __attribute__((packed));

struct vive_controller_touch_sample
{
	uint16_t touch[2];
} __attribute__((packed));

struct vive_controller_trigger_sample
{
	uint8_t trigger;
} __attribute__((packed));

struct vive_controller_button_sample
{
	uint8_t buttons;
} __attribute__((packed));

struct vive_controller_battery_sample
{
	uint8_t battery;
} __attribute__((packed));

#define VIVE_IMU_RANGE_MODES_REPORT_ID 0x01

struct vive_imu_range_modes_report
{
	uint8_t id;
	uint8_t gyro_range;
	uint8_t accel_range;
	uint8_t unknown[61];
} __attribute__((packed));

#define VIVE_MAINBOARD_STATUS_REPORT_ID 0x03

struct vive_mainboard_status_report
{
	uint8_t id;
	uint16_t unknown;
	uint8_t len;
	uint16_t lens_separation;
	uint16_t reserved1;
	uint8_t button;
	uint8_t reserved2[3];
	uint8_t proximity_change;
	uint8_t reserved3;
	uint16_t proximity;
	uint16_t ipd;
	uint8_t reserved4[46];
} __attribute__((packed));

#define VIVE_HEADSET_POWER_REPORT_ID 0x04

#define VIVE_HEADSET_POWER_REPORT_TYPE 0x2978

struct vive_headset_power_report
{
	uint8_t id;
	uint16_t type;
	uint8_t len;
	uint8_t unknown1[9];
	uint8_t reserved1[32];
	uint8_t unknown2;
	uint8_t reserved2[18];
} __attribute__((packed));

#define VIVE_HEADSET_MAINBOARD_DEVICE_INFO_REPORT_ID 0x04

#define VIVE_HEADSET_MAINBOARD_DEVICE_INFO_REPORT_TYPE 0x2987

struct vive_headset_mainboard_device_info_report
{
	uint8_t id;
	uint16_t type;
	uint8_t len;
	uint16_t edid_vid;
	uint16_t edid_pid;
	uint8_t unknown1[4];
	uint32_t display_firmware_version;
	uint8_t unknown2[48];
} __attribute__((packed));

#define VIVE_FIRMWARE_VERSION_REPORT_ID 0x05

struct vive_firmware_version_report
{
	uint8_t id;
	uint32_t firmware_version;
	uint32_t unknown1;
	uint8_t string1[16];
	uint8_t string2[16];
	uint8_t hardware_version_micro;
	uint8_t hardware_version_minor;
	uint8_t hardware_version_major;
	uint8_t hardware_revision;
	uint32_t unknown2;
	uint8_t fpga_version_minor;
	uint8_t fpga_version_major;
	uint8_t reserved[13];
} __attribute__((packed));

#define VIVE_CONFIG_START_REPORT_ID 0x10

struct vive_config_start_report
{
	uint8_t id;
	uint8_t unused[63];
} __attribute__((packed));

#define VIVE_CONFIG_READ_REPORT_ID 0x11

struct vive_config_read_report
{
	uint8_t id;
	uint8_t len;
	uint8_t payload[62];
} __attribute__((packed));

#define VIVE_IMU_REPORT_ID 0x20

struct vive_imu_sample
{
	uint16_t acc[3];
	uint16_t gyro[3];
	uint32_t time;
	uint8_t seq;
} __attribute__((packed));

struct vive_imu_report
{
	uint8_t id;
	struct vive_imu_sample sample[3];
} __attribute__((packed));

struct watchman_imu_sample
{
	/* ouvrt: "Time in 48 MHz ticks, but we are missing the low byte."
	 *
	 * The full timestamp is 4 bytes, formed by
	 * first byte : vive_controller_message.timestamp_hi
	 * second byte: vive_controller_message.timestamp_lo
	 * third byte: watchman_imu_sample.timestamp_hi
	 * fourth byte: remains zero */
	uint8_t timestamp_hi;
	uint16_t acc[3];
	uint16_t gyro[3];
} __attribute__((packed));


#define TYPE_FLAG_TOUCH_FORCE 161
struct watchman_touch_force
{
	uint8_t type_flag;

	uint8_t touch; // bitmask of touched buttons

	// "distance" from hardware
	uint8_t middle_finger_handle;
	uint8_t ring_finger_handle;
	uint8_t pinky_finger_handle;
	uint8_t index_finger_trigger;

	uint8_t squeeze_force;
	uint8_t trackpad_force;
} __attribute__((packed));

#define VIVE_CONTROLLER_LIGHTHOUSE_PULSE_REPORT_ID 0x21

struct vive_controller_lighthouse_pulse
{
	uint16_t id;
	uint16_t duration;
	uint32_t timestamp;
} __attribute__((packed));

struct vive_controller_lighthouse_pulse_report
{
	uint8_t id;
	struct vive_controller_lighthouse_pulse pulse[7];
	uint8_t reserved;
} __attribute__((packed));

#define VIVE_CONTROLLER_REPORT1_ID 0x23

#define VIVE_CONTROLLER_BATTERY_CHARGING 0x80
#define VIVE_CONTROLLER_BATTERY_CHARGE_MASK 0x7f

#define VIVE_CONTROLLER_BUTTON_TRIGGER 0x01
#define VIVE_CONTROLLER_BUTTON_TOUCH 0x02
#define VIVE_CONTROLLER_BUTTON_THUMB 0x04
#define VIVE_CONTROLLER_BUTTON_SYSTEM 0x08
#define VIVE_CONTROLLER_BUTTON_GRIP 0x10
#define VIVE_CONTROLLER_BUTTON_MENU 0x20

struct vive_controller_message
{
	uint8_t timestamp_hi;
	uint8_t len;
	uint8_t timestamp_lo;
	uint8_t payload[26];
} __attribute__((packed));

struct vive_controller_report1
{
	uint8_t id;
	struct vive_controller_message message;
} __attribute__((packed));

#define VIVE_CONTROLLER_REPORT2_ID 0x24

struct vive_controller_report2
{
	uint8_t id;
	struct vive_controller_message message[2];
} __attribute__((packed));

#define VIVE_HEADSET_LIGHTHOUSE_PULSE_REPORT_ID 0x25

struct vive_headset_lighthouse_v2_pulse
{
	uint8_t sensor_id;
	uint32_t timestamp;
	uint32_t data;
	uint32_t mask;
} __attribute__((packed));

#define VIVE_HEADSET_LIGHTHOUSE_V2_PULSE_REPORT_ID 0x27

struct vive_headset_lighthouse_v2_pulse_report
{
	uint8_t id;
	struct vive_headset_lighthouse_v2_pulse pulse[4];
	/* Seen to be all values in range [0 - 53], related to hit sensor (and
	 * imu?). */
	uint8_t unknown1;
	/* Always 0 */
	uint8_t unknown2;
	/* Always 0xde40daa */
	uint32_t unknown3;

} __attribute__((packed));

struct vive_headset_lighthouse_pulse
{
	uint8_t id;
	uint16_t duration;
	uint32_t timestamp;
} __attribute__((packed));

struct vive_headset_lighthouse_pulse_report
{
	uint8_t id;
	struct vive_headset_lighthouse_pulse pulse[9];
} __attribute__((packed));

#define VIVE_CONTROLLER_DISCONNECT_REPORT_ID 0x26

#define VIVE_CONTROLLER_COMMAND_REPORT_ID 0xff

#define VIVE_CONTROLLER_HAPTIC_PULSE_COMMAND 0x8f

struct vive_controller_haptic_pulse_report
{
	uint8_t id;
	uint8_t command;
	uint8_t len;
	uint8_t zero;
	uint16_t pulse_high;
	uint16_t pulse_low;
	uint16_t repeat_count;
} __attribute__((packed));

#define VIVE_CONTROLLER_POWEROFF_COMMAND 0x9f

struct vive_controller_poweroff_report
{
	uint8_t id;
	uint8_t command;
	uint8_t len;
	uint8_t magic[4];
} __attribute__((packed));

extern const struct vive_headset_power_report power_on_report;
extern const struct vive_headset_power_report power_off_report;

char *
vive_read_config(struct os_hid_device *hid_dev);

int
vive_get_imu_range_report(struct os_hid_device *hid_dev, double *gyro_range, double *acc_range);

int
vive_read_firmware(struct os_hid_device *hid_dev,
                   uint32_t *firmware_version,
                   uint8_t *hardware_revision,
                   uint8_t *hardware_version_micro,
                   uint8_t *hardware_version_minor,
                   uint8_t *hardware_version_major);
