/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019 Lucas Teske <lucas@teske.com.br>
 * Copyright 2019-2020 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 * OpenHMD - Free and Open Source API and drivers for immersive technology.
 */

/*!
 * @file
 * @brief  Oculus Rift S USB protocol implementation
 *
 * Functions for interpreting the USB protocol to the
 * headset and Touch Controllers (via the headset's radio link)
 *
 * Ported from OpenHMD
 *
 * @author Jan Schmidt <jan@centricular.com>
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/os_hid.h"
#include "os/os_time.h"

#include "xrt/xrt_defines.h"

#include "rift_s.h"
#include "rift_s_protocol.h"

/* FIXME: The code in this file is not portable to big-endian as-is - it needs endian swaps */
bool
rift_s_parse_hmd_report(rift_s_hmd_report_t *report, const unsigned char *buf, int size)
{
	if (buf[0] != 0x65)
		return false;

	if (size != 64 || size != sizeof(rift_s_hmd_report_t))
		return false;

	*report = *(rift_s_hmd_report_t *)(buf);

	return true;
}

bool
rift_s_parse_controller_report(rift_s_controller_report_t *report, const unsigned char *buf, int size)
{
	uint8_t avail;

	if (buf[0] != 0x67)
		return false;

	if (size < 62) {
		RIFT_S_WARN("Controller report with size %d - please report it", size);
		return false;
	}

	report->id = buf[0];
	report->device_id = *(uint64_t *)(buf + 1);
	report->data_len = buf[9];
	report->num_info = 0;
	report->extra_bytes_len = 0;
	report->flags = 0;
	memset(report->log, 0, sizeof(report->log));

	if (report->data_len < 4) {
		if (report->data_len != 0)
			RIFT_S_WARN("Controller report with data len %u - please report it", report->data_len);
		return true; // No more to read
	}

	/* Advance the buffer pointer to the end of the common header.
	 * We now have data_len bytes left to read
	 */
	buf += 10;
	size -= 10;

	if (report->data_len > size) {
		RIFT_S_WARN("Controller report with data len %u > packet size 62 - please report it", report->data_len);
		report->data_len = size;
	}

	avail = report->data_len;

	report->flags = buf[0];
	report->log[0] = buf[1];
	report->log[1] = buf[2];
	report->log[2] = buf[3];
	buf += 4;
	avail -= 4;

	/* While we have at least 2 bytes (type + at least 1 byte data), read a block */
	while (avail > 1 && report->num_info < sizeof(report->info) / sizeof(report->info[0])) {
		rift_s_controller_info_block_t *info = report->info + report->num_info;
		size_t block_size = 0;
		info->block_id = buf[0];

		switch (info->block_id) {
		case RIFT_S_CTRL_MASK08:
		case RIFT_S_CTRL_BUTTONS:
		case RIFT_S_CTRL_FINGERS:
		case RIFT_S_CTRL_MASK0e: block_size = sizeof(rift_s_controller_maskbyte_block_t); break;
		case RIFT_S_CTRL_TRIGGRIP: block_size = sizeof(rift_s_controller_triggrip_block_t); break;
		case RIFT_S_CTRL_JOYSTICK: block_size = sizeof(rift_s_controller_joystick_block_t); break;
		case RIFT_S_CTRL_CAPSENSE: block_size = sizeof(rift_s_controller_capsense_block_t); break;
		case RIFT_S_CTRL_IMU: block_size = sizeof(rift_s_controller_imu_block_t); break;
		default: break;
		}

		if (block_size == 0 || avail < block_size)
			break; /* Invalid block, or not enough data */

		memcpy(info->raw.data, buf, block_size);
		buf += block_size;
		avail -= block_size;
		report->num_info++;
	}

	if (avail > 0) {
		assert(avail < sizeof(report->extra_bytes));
		report->extra_bytes_len = avail;
		memcpy(report->extra_bytes, buf, avail);
	}

	return true;
}

int
rift_s_snprintf_hexdump_buffer(char *outbuf, size_t outbufsize, const char *label, const unsigned char *buf, int length)
{
	int indent = 0;
	char ascii[17];
	int printed = 0;

	if (label)
		indent = strlen(label) + 2;
	printed += snprintf(outbuf + printed, outbufsize - printed, "%s: ", label);

	ascii[16] = '\0';
	for (int i = 0; i < length; i++) {
		printed += snprintf(outbuf + printed, outbufsize - printed, "%02x ", buf[i]);

		if (buf[i] >= ' ' && buf[i] <= '~')
			ascii[i % 16] = buf[i];
		else
			ascii[i % 16] = '.';

		if ((i % 16) == 15 || (i + 1) == length) {
			if ((i % 16) < 15) {
				int remain = 15 - (i % 16);
				ascii[(i + 1) % 16] = '\0';
				/* Pad the hex dump out to 48 chars */
				printed += snprintf(outbuf + printed, outbufsize - printed, "%*s", 3 * remain, " ");
			}
			printed += snprintf(outbuf + printed, outbufsize - printed, "| %s", ascii);

			if ((i + 1) != length)
				printed += snprintf(outbuf + printed, outbufsize - printed, "\n%*s", indent, " ");
		}
	}

	return printed;
}

void
rift_s_hexdump_buffer(const char *label, const unsigned char *buf, int length)
{
	char outbuf[16384] = "";
	int bufsize = sizeof(outbuf) - 2;
	int printed = 0;

	printed += rift_s_snprintf_hexdump_buffer(outbuf, bufsize - printed, label, buf, length);

	RIFT_S_DEBUG("%s", outbuf);
}

static int
get_feature_report(struct os_hid_device *hid, uint8_t cmd, uint8_t *buf, int len)
{
	memset(buf, 0, len);
	buf[0] = cmd;
	return os_hid_get_feature(hid, buf[0], buf, len);
}

static int
read_one_fw_block(struct os_hid_device *dev, uint8_t block_id, uint32_t pos, uint8_t read_len, uint8_t *buf)
{
	unsigned char req[64] = {
	    0x4a,
	    0x00,
	};
	int ret, loops = 0;
	bool send_req = true;

	req[2] = block_id;

	do {
		if (send_req) {
			/* FIXME: Little-endian code: */
			*(uint32_t *)(req + 3) = pos;
			req[7] = read_len;
			ret = os_hid_set_feature(dev, req, 64);
			if (ret < 0) {
				RIFT_S_ERROR("Report 74 SET failed");
				return ret;
			}
		}

		ret = get_feature_report(dev, 0x4A, buf, 64);
		if (ret < 0) {
			RIFT_S_ERROR("Report 74 GET failed");
			return ret;
		}
		/* Loop until the result matches the address we asked for and
		 * the 2nd byte == 0x00 (0x1 = busy or req ignored?), or 20 attempts have passed */
		if (memcmp(req, buf, 7) == 0)
			break;

		/* Or if the 2nd byte of the return result is 0x1, the read is being processed,
		 * don't send the req again. If it's 0x00, we seem to need to re-send the request	*/
		send_req = (buf[1] == 0x00);

		/* FIXME: Avoid the sleep and just try again later? */
		os_nanosleep(U_TIME_1MS_IN_NS * 2);
	} while (loops++ < 20);

	if (loops > 20)
		return -1;

	return ret;
}

int
rift_s_read_firmware_block(struct os_hid_device *dev, uint8_t block_id, char **data_out, int *len_out)
{
	uint32_t pos = 0x00, block_len;
	unsigned char buf[64] = {
	    0x4a,
	    0x00,
	};
	unsigned char *outbuf;
	size_t total_read = 0;
	int ret;

	ret = read_one_fw_block(dev, block_id, 0, 0xC, buf);
	if (ret < 0) {
		RIFT_S_ERROR("Failed to read fw block %02x header", block_id);
		return ret;
	}

	/* The block header is 12 bytes. 8 byte checksum, 4 byte size? */
	block_len = *(uint32_t *)(buf + 16);

	if (block_len < 0xC || block_len == 0xFFFFFFFF)
		return -1; /* Invalid block */

#if 0
	uint64_t checksum = *(uint64_t *)(buf + 8);
	printf ("FW Block %02x Header. Checksum(?) %08lx len %d\n", block_id, checksum, block_len);
#endif

	/* Copy the contents of the fw block, minus the header */
	outbuf = malloc(block_len + 1);
	outbuf[block_len] = 0;
	total_read = 0x0;

	for (pos = 0x0; pos < block_len; pos += 56) {
		uint8_t read_len = 56;
		if (pos + read_len > block_len)
			read_len = block_len - pos;

		ret = read_one_fw_block(dev, block_id, pos + 0xC, read_len, buf);
		if (ret < 0) {
			RIFT_S_ERROR("Failed to read fw block %02x at pos 0x%08x len %d", block_id, pos, read_len);
			free(outbuf);
			return ret;
		}
		memcpy(outbuf + total_read, buf + 8, read_len);
		total_read += read_len;
	}

	if (total_read > 0) {
		if (total_read < block_len) {
			RIFT_S_ERROR("Short FW read - only read %u bytes of %u", (unsigned int)total_read, block_len);
			free(outbuf);
			return -1;
		}

#if 0
		char label[64];
		sprintf (label, "FW Block %02x", block_id);
		if (outbuf[0] == '{' && outbuf[total_read-2] == '}' && outbuf[total_read-1] == 0)
			printf ("%s\n", outbuf); // Dump JSON string
		else
			rift_s_hexdump_buffer (label, outbuf, total_read);
#endif
	}

	*data_out = (char *)(outbuf);
	*len_out = block_len;

	return ret;
}

void
rift_s_send_keepalive(struct os_hid_device *hid)
{
	/* HID report 147 (0x93) 0xbb8 = 3000ms timeout, sent every 1000ms */
	unsigned char buf[6] = {0x93, 0x01, 0xb8, 0x0b, 0x00, 0x00};
	os_hid_set_feature(hid, buf, 6);
}

void
rift_s_protocol_camera_report_init(rift_s_camera_report_t *camera_report)
{
	int i;

	/* One slot per camera: */
	camera_report->id = 0x05;
	camera_report->uvc_enable = 0x0;
	camera_report->radio_sync_flag = 0x0;

	camera_report->marker[0] = 0x26;
	camera_report->marker[1] = 0x0;
	camera_report->marker[2] = 0x40;

	for (i = 0; i < 5; i++) {
		camera_report->slam_frame_exposures[i] = 0x36b3;
		camera_report->slam_frame_gains[i] = 0xf0;
		camera_report->unknown32[i] = 0x04bc;
	}
}

int
rift_s_protocol_send_camera_report(struct os_hid_device *hid, rift_s_camera_report_t *camera_report)
{
	return os_hid_set_feature(hid, (uint8_t *)camera_report, sizeof(*camera_report));
}

static int
rift_s_enable_camera(struct os_hid_device *hid, bool enable, bool radio_sync_bit)
{
	rift_s_camera_report_t camera_report;

	rift_s_protocol_camera_report_init(&camera_report);

	camera_report.uvc_enable = enable ? 0x1 : 0x0;
	camera_report.radio_sync_flag = radio_sync_bit ? 0x1 : 0x0;

	return rift_s_protocol_send_camera_report(hid, &camera_report);
}

int
rift_s_set_screen_enable(struct os_hid_device *hid, bool enable)
{
	uint8_t buf[2];

	// Enable/disable LCD screen
	buf[0] = 0x08;
	buf[1] = enable ? 0x01 : 0;
	return os_hid_set_feature(hid, buf, 2);
}

int
rift_s_read_panel_info(struct os_hid_device *hid, rift_s_panel_info_t *panel_info)
{
	uint8_t buf[FEATURE_BUFFER_SIZE];

	int res = get_feature_report(hid, 0x06, buf, FEATURE_BUFFER_SIZE);
	if (res < (int)sizeof(rift_s_panel_info_t)) {
		RIFT_S_ERROR("Failed to read %d bytes of panel info", FEATURE_BUFFER_SIZE);
		return res;
	}
	rift_s_hexdump_buffer("panel info", buf, res);

	*panel_info = *(rift_s_panel_info_t *)buf;

	return 0;
}

int
rift_s_read_firmware_version(struct os_hid_device *hid)
{
	uint8_t buf[FEATURE_BUFFER_SIZE];
	int res;

	res = get_feature_report(hid, 0x01, buf, 43);
	if (res < 0) {
		return res;
	}

	rift_s_hexdump_buffer("Firmware version", buf, res);
	return 0;
}

int
rift_s_read_imu_config_info(struct os_hid_device *hid, struct rift_s_imu_config_info_t *imu_config)
{
	uint8_t buf[FEATURE_BUFFER_SIZE];
	int res;

	res = get_feature_report(hid, 0x09, buf, FEATURE_BUFFER_SIZE);
	if (res < 21)
		return -1;

	*imu_config = *(struct rift_s_imu_config_info_t *)buf;

	return 0;
}

int
rift_s_protocol_set_proximity_threshold(struct os_hid_device *hid, uint16_t threshold)
{
	uint8_t buf[3];

	/* Proximity sensor threshold 0x07 */
	buf[0] = 0x07;
	buf[1] = threshold & 0xff;
	buf[2] = (threshold >> 8) & 0xff;

	return os_hid_set_feature(hid, buf, 3);
}

int
rift_s_hmd_enable(struct os_hid_device *hid, bool enable)
{
	uint8_t buf[3];
	int res;

	/* Enable device */
	buf[0] = 0x14;
	buf[1] = enable ? 0x01 : 0x00;
	if ((res = os_hid_set_feature(hid, buf, 2)) < 0)
		return res;

	/* Turn on radio to controllers */
	buf[0] = 0x0A;
	buf[1] = enable ? 0x02 : 0x00;
	if ((res = os_hid_set_feature(hid, buf, 2)) < 0)
		return res;

	if (!enable) {
		/* Shutting off - turn off the LCD */
		res = rift_s_set_screen_enable(hid, false);
		if (res < 0)
			return res;
	}

	/* Enables prox sensor + HMD IMU etc */
	buf[0] = 0x02;
	buf[1] = enable ? 0x01 : 0x00;
	if ((res = os_hid_set_feature(hid, buf, 2)) < 0)
		return res;

	/* Send camera report with enable=true enables the streaming. The
	 * 2nd byte seems something to do with sync, but doesn't always work,
	 * not sure why yet. */
	return rift_s_enable_camera(hid, enable, false);
}

/* Read the list of devices on the radio link */
int
rift_s_read_devices_list(struct os_hid_device *handle, rift_s_devices_list_t *dev_list)
{
	unsigned char buf[200];

	int res = get_feature_report(handle, 0x0c, buf, sizeof(buf));
	if (res < 3) {
		/* This happens when the Rift is just starting, we'll try again later */
		return -1;
	}

	int num_records = (res - 3) / 28;
	if (num_records > buf[2])
		num_records = buf[2];
	if (num_records > DEVICES_LIST_MAX_DEVICES)
		num_records = DEVICES_LIST_MAX_DEVICES;

	unsigned char *pos = buf + 3;

	for (int i = 0; i < num_records; i++) {
		dev_list->devices[i] = *(rift_s_device_type_record_t *)(pos);
		pos += sizeof(rift_s_device_type_record_t);
		assert(sizeof(rift_s_device_type_record_t) == 28);
	}
	dev_list->num_devices = num_records;

	return 0;
}
