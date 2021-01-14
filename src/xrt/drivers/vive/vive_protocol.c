// Copyright 2016-2019, Philipp Zabel
// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vive USB HID reports
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#include "math/m_mathinclude.h"

#include <stdio.h>
#include <zlib.h>
#include "math/m_api.h"

#include "vive_protocol.h"

#include "util/u_debug.h"
#include "util/u_misc.h"
#include "util/u_json.h"
#include "util/u_logging.h"

const struct vive_headset_power_report power_on_report = {
    .id = VIVE_HEADSET_POWER_REPORT_ID,
    .type = __cpu_to_le16(VIVE_HEADSET_POWER_REPORT_TYPE),
    .len = 56,
    .unknown1 =
        {
            0x01,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x02,
            0x00,
            0x01,
        },
    .unknown2 = 0x7a,
};

const struct vive_headset_power_report power_off_report = {
    .id = VIVE_HEADSET_POWER_REPORT_ID,
    .type = __cpu_to_le16(VIVE_HEADSET_POWER_REPORT_TYPE),
    .len = 56,
    .unknown1 =
        {
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x02,
            0x00,
            0x00,
        },
    .unknown2 = 0x7c,
};


char *
vive_read_config(struct os_hid_device *hid_dev)
{
	struct vive_config_start_report start_report = {
	    .id = VIVE_CONFIG_START_REPORT_ID,
	};

	int ret = os_hid_get_feature_timeout(hid_dev, &start_report, sizeof(start_report), 100);
	if (ret < 0) {
		U_LOG_E("Could not get config start report.");
		return NULL;
	}

	struct vive_config_read_report report = {
	    .id = VIVE_CONFIG_READ_REPORT_ID,
	};

	unsigned char *config_z = U_TYPED_ARRAY_CALLOC(unsigned char, 4096);

	uint32_t count = 0;
	do {
		ret = os_hid_get_feature_timeout(hid_dev, &report, sizeof(report), 100);
		if (ret < 0) {
			U_LOG_E("Read error after %d bytes: %d", count, ret);
			free(config_z);
			return NULL;
		}

		if (report.len > 62) {
			U_LOG_E("Invalid configuration data at %d", count);
			free(config_z);
			return NULL;
		}

		if (count + report.len > 4096) {
			U_LOG_E("Configuration data too large");
			free(config_z);
			return NULL;
		}

		memcpy(config_z + count, report.payload, report.len);
		count += report.len;
	} while (report.len);

	unsigned char *config_json = U_TYPED_ARRAY_CALLOC(unsigned char, 32768);

	z_stream strm = {
	    .next_in = config_z,
	    .avail_in = count,
	    .next_out = config_json,
	    .avail_out = 32768,
	    .zalloc = Z_NULL,
	    .zfree = Z_NULL,
	    .opaque = Z_NULL,
	};

	ret = inflateInit(&strm);
	if (ret != Z_OK) {
		U_LOG_E("inflate_init failed: %d", ret);
		free(config_z);
		free(config_json);
		return NULL;
	}

	ret = inflate(&strm, Z_FINISH);
	free(config_z);
	if (ret != Z_STREAM_END) {
		U_LOG_E("Failed to inflate configuration data: %d", ret);
		free(config_json);
		return NULL;
	}

	config_json[strm.total_out] = '\0';

	U_ARRAY_REALLOC_OR_FREE(config_json, unsigned char, strm.total_out + 1);

	inflateEnd(&strm);

	return (char *)config_json;
}

int
vive_get_imu_range_report(struct os_hid_device *hid_dev, double *gyro_range, double *acc_range)
{
	struct vive_imu_range_modes_report report = {.id = VIVE_IMU_RANGE_MODES_REPORT_ID};

	int ret;

	ret = os_hid_get_feature_timeout(hid_dev, &report, sizeof(report), 100);
	if (ret < 0) {
		U_LOG_E("Could not get range report!");
		return ret;
	}

	if (!report.gyro_range || !report.accel_range) {
		U_LOG_W(
		    "Invalid gyroscope and accelerometer data."
		    "Trying to fetch again.");
		ret = os_hid_get_feature(hid_dev, report.id, (uint8_t *)&report, sizeof(report));
		if (ret < 0) {
			U_LOG_E("Could not get feature report %d.", report.id);
			return ret;
		}

		if (!report.gyro_range || !report.accel_range) {
			U_LOG_E("Unexpected range mode report: %02x %02x %02x", report.id, report.gyro_range,
			        report.accel_range);
			for (int i = 0; i < 61; i++)
				printf(" %02x", report.unknown[i]);
			printf("\n");
			return -1;
		}
	}

	if (report.gyro_range > 4 || report.accel_range > 4) {
		U_LOG_W("Gyroscope or accelerometer range too large.");
		U_LOG_W("Gyroscope: %d", report.gyro_range);
		U_LOG_W("Accelerometer: %d", report.accel_range);
		return -1;
	}

	/*
	 * Convert MPU-6500 gyro full scale range (+/-250°/s, +/-500°/s,
	 * +/-1000°/s, or +/-2000°/s) into rad/s, accel full scale range
	 * (+/-2g, +/-4g, +/-8g, or +/-16g) into m/s².
	 */

	*gyro_range = M_PI / 180.0 * (250 << report.gyro_range);
	*acc_range = MATH_GRAVITY_M_S2 * (2 << report.accel_range);

	return 0;
}

int
vive_read_firmware(struct os_hid_device *hid_dev,
                   uint32_t *firmware_version,
                   uint8_t *hardware_revision,
                   uint8_t *hardware_version_micro,
                   uint8_t *hardware_version_minor,
                   uint8_t *hardware_version_major)
{
	struct vive_firmware_version_report report = {
	    .id = VIVE_FIRMWARE_VERSION_REPORT_ID,
	};

	int ret;
	ret = os_hid_get_feature(hid_dev, report.id, (uint8_t *)&report, sizeof(report));
	if (ret < 0)
		return ret;

	*firmware_version = __le32_to_cpu(report.firmware_version);
	*hardware_revision = report.hardware_revision;
	*hardware_version_major = report.hardware_version_major;
	*hardware_version_minor = report.hardware_version_minor;
	*hardware_version_micro = report.hardware_version_micro;

	return 0;
}
