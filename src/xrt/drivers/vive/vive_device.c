// Copyright 2016-2019, Philipp Zabel
// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vive device implementation
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <zlib.h>

#include "util/u_device.h"
#include "util/u_debug.h"
#include "util/u_var.h"

#include "math/m_api.h"

#include "os/os_hid.h"

#include "nxjson/nxjson.h"

#include "vive_device.h"
#include "vive_protocol.h"

#define VIVE_CLOCK_FREQ 48e6      // 48 MHz
#define XRT_GRAVITY_EARTH 9.80665 // m/s²

DEBUG_GET_ONCE_BOOL_OPTION(vive_spew, "VIVE_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(vive_debug, "VIVE_PRINT_DEBUG", false)

static bool
vive_mainboard_power_off(struct vive_device *d);

static inline struct vive_device *
vive_device(struct xrt_device *xdev)
{
	return (struct vive_device *)xdev;
}

static void
vive_device_destroy(struct xrt_device *xdev)
{
	struct vive_device *d = vive_device(xdev);
	vive_mainboard_power_off(d);

	// Destroy the thread object.
	os_thread_helper_destroy(&d->sensors_thread);
	os_thread_helper_destroy(&d->mainboard_thread);

	if (d->mainboard_dev != NULL) {
		os_hid_destroy(d->mainboard_dev);
		d->mainboard_dev = NULL;
	}

	if (d->sensors_dev != NULL) {
		os_hid_destroy(d->sensors_dev);
		d->sensors_dev = NULL;
	}

	// Remove the variable tracking.
	u_var_remove_root(d);

	free(d);
}

static void
vive_device_update_inputs(struct xrt_device *xdev,
                          struct time_state *timekeeping)
{
	struct vive_device *d = vive_device(xdev);
	VIVE_DEBUG(d, "vive_device_update_inputs.");
}

static void
vive_device_get_tracked_pose(struct xrt_device *xdev,
                             enum xrt_input_name name,
                             struct time_state *timekeeping,
                             int64_t *out_timestamp,
                             struct xrt_space_relation *out_relation)
{
	struct vive_device *d = vive_device(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_RELATION) {
		VIVE_ERROR("unknown input name");
		return;
	}

	// Clear out the relation.
	U_ZERO(out_relation);

	int64_t when = time_state_get_now(timekeeping);
	*out_timestamp = when;

	os_thread_helper_lock(&d->sensors_thread);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&d->sensors_thread)) {
		os_thread_helper_unlock(&d->sensors_thread);
		return;
	}

	out_relation->pose.orientation = d->rot_filtered;

	//! @todo assuming that orientation is actually currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_POSITION_TRACKED_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

	os_thread_helper_unlock(&d->sensors_thread);
}

static void
vive_device_get_view_pose(struct xrt_device *xdev,
                          struct xrt_vec3 *eye_relation,
                          uint32_t view_index,
                          struct xrt_pose *out_pose)
{
	struct xrt_pose pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
	bool adjust = view_index == 0;

	pose.position.x = eye_relation->x / 2.0f;
	pose.position.y = eye_relation->y / 2.0f;
	pose.position.z = eye_relation->z / 2.0f;

	// Adjust for left/right while also making sure there aren't any -0.f.
	if (pose.position.x > 0.0f && adjust) {
		pose.position.x = -pose.position.x;
	}
	if (pose.position.y > 0.0f && adjust) {
		pose.position.y = -pose.position.y;
	}
	if (pose.position.z > 0.0f && adjust) {
		pose.position.z = -pose.position.z;
	}

	*out_pose = pose;
}

static int
vive_mainboard_get_device_info(struct vive_device *d)
{
	struct vive_headset_mainboard_device_info_report report = {
	    .id = VIVE_HEADSET_MAINBOARD_DEVICE_INFO_REPORT_ID,
	};
	uint16_t edid_vid;
	uint16_t type;
	int ret;

	ret = os_hid_get_feature(d->mainboard_dev, report.id,
	                         (uint8_t *)&report, sizeof(report));
	if (ret < 0)
		return ret;

	type = __le16_to_cpu(report.type);
	if (type != VIVE_HEADSET_MAINBOARD_DEVICE_INFO_REPORT_TYPE ||
	    report.len != 60) {
		VIVE_ERROR("Unexpected device info!");
		return -1;
	}

	edid_vid = __be16_to_cpu(report.edid_vid);

	d->firmware.display_firmware_version =
	    __le32_to_cpu(report.display_firmware_version);

	VIVE_DEBUG(d, "EDID Manufacturer ID: %c%c%c, Product code: 0x%04x",
	           '@' + (edid_vid >> 10), '@' + ((edid_vid >> 5) & 0x1f),
	           '@' + (edid_vid & 0x1f), __le16_to_cpu(report.edid_pid));
	VIVE_DEBUG(d, "Display firmware version: %u",
	           d->firmware.display_firmware_version);

	return 0;
}


static bool
vive_mainboard_power_on(struct vive_device *d)
{
	int ret;
	ret = os_hid_set_feature(d->mainboard_dev,
	                         (const uint8_t *)&power_on_report,
	                         sizeof(power_on_report));
	VIVE_DEBUG(d, "Power on: %d", ret);
	return true;
}

static bool
vive_mainboard_power_off(struct vive_device *d)
{
	int ret;
	ret = os_hid_set_feature(d->mainboard_dev,
	                         (const uint8_t *)&power_off_report,
	                         sizeof(power_off_report));
	VIVE_DEBUG(d, "Power off: %d", ret);

	return true;
}

static bool
vive_mainboard_read_one_msg(struct vive_device *d, uint8_t *buffer, size_t size)
{
	os_thread_helper_lock(&d->mainboard_thread);

	while (os_thread_helper_is_running_locked(&d->mainboard_thread)) {

		os_thread_helper_unlock(&d->mainboard_thread);

		int ret = os_hid_read(d->mainboard_dev, buffer, size, 1000);
		if (ret == 0) {
			// Must lock thread before check in while.
			os_thread_helper_lock(&d->mainboard_thread);
			continue;
		} else if (ret < 0) {
			VIVE_ERROR("Failed to read device '%i'!", ret);
			return false;
		}

		return true;
	}

	return false;
}

static void
vive_mainboard_decode_message(struct vive_device *d,
                              struct vive_mainboard_status_report *report)
{
	uint16_t ipd;
	uint16_t lens_separation;
	uint16_t proximity;

	if (__le16_to_cpu(report->unknown) != 0x2cd0 || report->len != 60 ||
	    report->reserved1 || report->reserved2[0]) {
		VIVE_ERROR("Unexpected message content.");
	}

	ipd = __le16_to_cpu(report->ipd);
	lens_separation = __le16_to_cpu(report->lens_separation);
	proximity = __le16_to_cpu(report->proximity);

	if (d->board.ipd != ipd) {
		d->board.ipd = ipd;
		d->board.lens_separation = lens_separation;
		VIVE_SPEW(d, "IPD %4.1f mm. Lens separation %4.1f mm.",
		          1e-2 * ipd, 1e-2 * lens_separation);
	}

	if (d->board.proximity != proximity) {
		VIVE_SPEW(d, "Proximity %d", proximity);
		d->board.proximity = proximity;
	}

	if (d->board.button != report->button) {
		d->board.button = report->button;
		VIVE_SPEW(d, "Button %d.", report->button);
	}
}

static inline int
oldest_sequence_index(uint8_t a, uint8_t b, uint8_t c)
{
	if (a == (uint8_t)(b + 2))
		return 1;
	else if (b == (uint8_t)(c + 2))
		return 2;
	else
		return 0;
}

static void
update_imu(struct vive_device *d, struct vive_imu_report *report)
{
	const struct vive_imu_sample *sample = report->sample;
	uint8_t last_seq = d->imu.sequence;
	int i, j;

	/*
	 * The three samples are updated round-robin. New messages
	 * can contain already seen samples in any place, but the
	 * sequence numbers should always be consecutive.
	 * Start at the sample with the oldest sequence number.
	 */
	i = oldest_sequence_index(sample[0].seq, sample[1].seq, sample[2].seq);

	/* From there, handle all new samples */
	for (j = 3; j; --j, i = (i + 1) % 3) {
		uint32_t sample_time;
		float scale;
		uint8_t seq;
		int32_t dt;

		uint64_t raw_time;

		sample = report->sample + i;
		seq = sample->seq;

		/* Skip already seen samples */
		if (seq == last_seq || seq == (uint8_t)(last_seq - 1) ||
		    seq == (uint8_t)(last_seq - 2))
			continue;

		sample_time = __le32_to_cpu(sample->time);

		dt = sample_time - (uint32_t)d->imu.time;
		raw_time = d->imu.time + dt;

		int16_t acc[3] = {(int16_t)__le16_to_cpu(sample->acc[0]),
		                  (int16_t)__le16_to_cpu(sample->acc[1]),
		                  (int16_t)__le16_to_cpu(sample->acc[2])};

		scale = (float)d->imu.acc_range / 32768.0f;
		struct xrt_vec3 acceleration = {
		    -scale * d->imu.acc_scale.x * acc[0] - d->imu.acc_bias.x,
		    -scale * d->imu.acc_scale.y * acc[1] - d->imu.acc_bias.y,
		    -scale * d->imu.acc_scale.z * acc[2] - d->imu.acc_bias.z};

		int16_t gyro[3] = {(int16_t)__le16_to_cpu(sample->gyro[0]),
		                   (int16_t)__le16_to_cpu(sample->gyro[1]),
		                   (int16_t)__le16_to_cpu(sample->gyro[2])};

		scale = (float)d->imu.gyro_range / 32768.0f;
		struct xrt_vec3 angular_velocity = {
		    -scale * d->imu.gyro_scale.x * gyro[0] - d->imu.gyro_bias.x,
		    -scale * d->imu.gyro_scale.y * gyro[1] - d->imu.gyro_bias.y,
		    -scale * d->imu.gyro_scale.z * gyro[2] -
		        d->imu.gyro_bias.z};

		VIVE_SPEW(d, "ACC  %f %f %f", acceleration.x, acceleration.y,
		          acceleration.z);

		VIVE_SPEW(d, "GYRO %f %f %f", angular_velocity.x,
		          angular_velocity.y, angular_velocity.z);

		switch (d->variant) {
		case VIVE_VARIANT_VIVE:
			// flip x axis
			angular_velocity.x = -angular_velocity.x;
			break;
		case VIVE_VARIANT_PRO:
			// flip y axis
			angular_velocity.y = -angular_velocity.y;
			break;
		default: VIVE_ERROR("Unhandled Vive variant\n"); return;
		}

		math_quat_integrate_velocity(
		    &d->rot_filtered, &angular_velocity,
		    (float)(dt / VIVE_CLOCK_FREQ), &d->rot_filtered);

		d->imu.sequence = seq;
		d->imu.time = raw_time;
	}
}

static void *
vive_mainboard_run_thread(void *ptr)
{
	struct vive_device *d = (struct vive_device *)ptr;

	uint8_t buffer[64];

	while (vive_mainboard_read_one_msg(d, buffer, sizeof(buffer))) {
		if (buffer[0] == VIVE_MAINBOARD_STATUS_REPORT_ID) {
			struct vive_mainboard_status_report *report =
			    (void *)buffer;
			if (64 != sizeof(*report))
				VIVE_ERROR(
				    "Mainboard status report has invalid "
				    "size.");
			else
				vive_mainboard_decode_message(d, report);
		} else {
			VIVE_ERROR("Unknown message type %d", buffer[0]);
		}
	}

	return NULL;
}

static bool
vive_sensors_read_one_msg(struct vive_device *d)
{
	os_thread_helper_lock(&d->sensors_thread);

	uint8_t buffer[64];

	while (os_thread_helper_is_running_locked(&d->sensors_thread)) {

		os_thread_helper_unlock(&d->sensors_thread);

		int ret =
		    os_hid_read(d->sensors_dev, buffer, sizeof(buffer), 1000);

		if (ret == 0) {
			// Must lock thread before check in while.
			os_thread_helper_lock(&d->sensors_thread);
			continue;
		} else if (ret < 0) {
			VIVE_ERROR("Failed to read device '%i'!", ret);
			return false;
		}

		if (buffer[0] == VIVE_IMU_REPORT_ID) {
			if (ret != 52) {
				VIVE_ERROR("Wrong IMU report size: %d", ret);
				return false;
			} else {
				update_imu(d, (struct vive_imu_report *)buffer);
			}
		} else
			VIVE_ERROR("Unknown message type %d", buffer[0]);

		return true;
	}

	return false;
}

static void *
vive_sensors_run_thread(void *d)
{
	while (vive_sensors_read_one_msg((struct vive_device *)d))
		;
	return NULL;
}


int
vive_sensors_read_firmware(struct vive_device *d)
{
	struct vive_firmware_version_report report = {
	    .id = VIVE_FIRMWARE_VERSION_REPORT_ID,
	};

	int ret;
	ret = os_hid_get_feature(d->sensors_dev, report.id, (uint8_t *)&report,
	                         sizeof(report));
	if (ret < 0)
		return ret;

	d->firmware.firmware_version = __le32_to_cpu(report.firmware_version);
	d->firmware.hardware_revision = report.hardware_revision;

	VIVE_DEBUG(d, "Firmware version %u %s@%s FPGA %u.%u",
	           d->firmware.firmware_version, report.string1, report.string2,
	           report.fpga_version_major, report.fpga_version_minor);
	VIVE_DEBUG(d, "Hardware revision: %d rev %d.%d.%d",
	           d->firmware.hardware_revision, report.hardware_version_major,
	           report.hardware_version_minor,
	           report.hardware_version_micro);

	return 0;
}

int
vive_sensros_get_imu_range_report(struct vive_device *d)
{
	struct vive_imu_range_modes_report report = {
	    .id = VIVE_IMU_RANGE_MODES_REPORT_ID};

	int ret;
	ret = os_hid_get_feature(d->sensors_dev, report.id, (uint8_t *)&report,
	                         sizeof(report));
	if (ret < 0)
		return ret;

	if (!report.gyro_range || !report.accel_range) {
		VIVE_ERROR(
		    "Invalid gyroscope and accelerometer data. Trying to fetch "
		    "again.");
		ret = os_hid_get_feature(d->sensors_dev, report.id,
		                         (uint8_t *)&report, sizeof(report));
		if (ret < 0) {
			VIVE_ERROR("Could not get feature report %d.",
			           report.id);
			return ret;
		}

		if (!report.gyro_range || !report.accel_range) {
			VIVE_ERROR(
			    "Unexpected range mode report: %02x %02x %02x",
			    report.id, report.gyro_range, report.accel_range);
			for (int i = 0; i < 61; i++)
				printf(" %02x", report.unknown[i]);
			printf("\n");
			return -1;
		}
	}

	if (report.gyro_range > 4 || report.accel_range > 4) {
		VIVE_ERROR("Gyroscope or accelerometer range too large.");
		VIVE_ERROR("Gyroscope: %d", report.gyro_range);
		VIVE_ERROR("Aaccelerometer: %d", report.accel_range);
		d->imu.gyro_range = 8.726646f;
		d->imu.acc_range = 39.226600f;
		return -1;
	}

	/*
	 * Convert MPU-6500 gyro full scale range (+/-250°/s, +/-500°/s,
	 * +/-1000°/s, or +/-2000°/s) into rad/s, accel full scale range
	 * (+/-2g, +/-4g, +/-8g, or +/-16g) into m/s².
	 */

	d->imu.gyro_range = M_PI / 180.0 * (250 << report.gyro_range);
	VIVE_DEBUG(d, "Vive gyroscope range     %f", d->imu.gyro_range);

	d->imu.acc_range = XRT_GRAVITY_EARTH * (2 << report.accel_range);
	VIVE_DEBUG(d, "Vive accelerometer range %f", d->imu.acc_range);

	return 0;
}


void
print_vec3(const char *title, struct xrt_vec3 *vec)
{
	printf("%s = %f %f %f\n", title, (double)vec->x, (double)vec->y,
	       (double)vec->z);
}


static void
_array_to_vec3(float array[3], struct xrt_vec3 *result)
{
	result->x = array[0];
	result->y = array[1];
	result->z = array[2];
}

static void
_json_get_vec3(const nx_json *json, const char *name, struct xrt_vec3 *result)
{
	const nx_json *acc_bias_arr = nx_json_get(json, name);

	float result_array[3];

	assert(acc_bias_arr->length == 3);

	for (int i = 0; i < acc_bias_arr->length; i++) {
		const nx_json *item = nx_json_item(acc_bias_arr, i);
		result_array[i] = (float)item->dbl_value;
	}

	_array_to_vec3(result_array, result);
}

static char *
_json_get_string(const nx_json *json, const char *name)
{
	const nx_json *item = nx_json_get(json, name);
	return strdup(item->text_value);
}

static double
_json_get_double(const nx_json *json, const char *name)
{
	const nx_json *item = nx_json_get(json, name);
	return item->dbl_value;
}

static float
_json_get_float(const nx_json *json, const char *name)
{
	const nx_json *item = nx_json_get(json, name);
	return (float)item->dbl_value;
}

static long long
_json_get_int(const nx_json *json, const char *name)
{
	const nx_json *item = nx_json_get(json, name);
	return item->int_value;
}

static void
_get_color_coeffs(struct xrt_hmd_parts *hmd,
                  const nx_json *coeffs,
                  uint8_t eye,
                  uint8_t channel)
{
	assert(coeffs->length == 8);
	// only 3 coeffs contain values
	for (int i = 0; i < 3; i++) {
		const nx_json *item = nx_json_item(coeffs, i);
		hmd->distortion.vive.coefficients[eye][i][channel] =
		    (float)item->dbl_value;
	}
}

static void
get_distortion_properties(struct xrt_hmd_parts *hmd,
                          const nx_json *eye_transform_json,
                          uint8_t eye)
{
	const nx_json *eye_json = nx_json_item(eye_transform_json, eye);
	if (eye_json) {
		// TODO: store grow_for_undistort per eye
		hmd->distortion.vive.grow_for_undistort =
		    _json_get_float(eye_json, "grow_for_undistort");
		hmd->distortion.vive.undistort_r2_cutoff[eye] =
		    _json_get_float(eye_json, "undistort_r2_cutoff");

		const nx_json *distortion = nx_json_get(eye_json, "distortion");
		if (distortion) {
			// TODO: store center per color
			hmd->distortion.vive.center[eye][0] =
			    _json_get_float(eye_json, "center_x");
			hmd->distortion.vive.center[eye][1] =
			    _json_get_float(eye_json, "center_y");

			// green
			const nx_json *coeffs =
			    nx_json_get(distortion, "coeffs");
			if (coeffs)
				_get_color_coeffs(hmd, coeffs, eye, 1);
		}

		const nx_json *distortion_blue =
		    nx_json_get(eye_json, "distortion_blue");
		if (distortion_blue) {
			const nx_json *coeffs =
			    nx_json_get(distortion_blue, "coeffs");
			// blue
			if (coeffs)
				_get_color_coeffs(hmd, coeffs, eye, 2);
		}

		const nx_json *distortion_red =
		    nx_json_get(eye_json, "distortion_red");
		if (distortion_red) {
			const nx_json *coeffs =
			    nx_json_get(distortion_red, "coeffs");
			// red
			if (coeffs)
				_get_color_coeffs(hmd, coeffs, eye, 0);
		}
	}
}

bool
vive_parse_config(struct vive_device *d, char *json_string)
{
	VIVE_SPEW(d, "JSON config:\n%s\n", json_string);

	const nx_json *json = nx_json_parse(json_string, 0);
	if (json) {

		switch (d->variant) {
		case VIVE_VARIANT_VIVE:
			_json_get_vec3(json, "acc_bias", &d->imu.acc_bias);
			_json_get_vec3(json, "acc_scale", &d->imu.acc_scale);
			_json_get_vec3(json, "gyro_bias", &d->imu.gyro_bias);
			_json_get_vec3(json, "gyro_scale", &d->imu.gyro_scale);
			break;
		case VIVE_VARIANT_PRO: {
			const nx_json *imu = nx_json_get(json, "imu");
			_json_get_vec3(imu, "acc_bias", &d->imu.acc_bias);
			_json_get_vec3(imu, "acc_scale", &d->imu.acc_scale);
			_json_get_vec3(imu, "gyro_bias", &d->imu.gyro_bias);
			_json_get_vec3(imu, "gyro_scale", &d->imu.gyro_scale);
		} break;
		default: VIVE_ERROR("Unknown Vive variant.\n"); return false;
		}

		d->firmware.model_number =
		    _json_get_string(json, "model_number");
		d->firmware.mb_serial_number =
		    _json_get_string(json, "mb_serial_number");
		d->display.lens_separation =
		    _json_get_double(json, "lens_separation");
		d->firmware.device_serial_number =
		    _json_get_string(json, "device_serial_number");

		const nx_json *device_json = nx_json_get(json, "device");
		if (device_json) {
			d->display.persistence =
			    _json_get_double(device_json, "persistence");
			d->base.hmd->distortion.vive.aspect_x_over_y =
			    _json_get_float(device_json,
			                    "physical_aspect_x_over_y");
			d->display.eye_target_height_in_pixels =
			    (uint16_t)_json_get_int(
			        device_json, "eye_target_height_in_pixels");
			d->display.eye_target_width_in_pixels =
			    (uint16_t)_json_get_int(
			        device_json, "eye_target_width_in_pixels");
		}

		const nx_json *eye_transform_json =
		    nx_json_get(json, "tracking_to_eye_transform");
		if (eye_transform_json) {
			for (uint8_t eye = 0; eye < 2; eye++)
				get_distortion_properties(
				    d->base.hmd, eye_transform_json, eye);
		}

		nx_json_free(json);

		VIVE_DEBUG(d, "= Vive configuration =");
		VIVE_DEBUG(d, "lens_separation: %f",
		           d->display.lens_separation);
		VIVE_DEBUG(d, "persistence: %f", d->display.persistence);
		VIVE_DEBUG(
		    d, "physical_aspect_x_over_y: %f",
		    (double)d->base.hmd->distortion.vive.aspect_x_over_y);

		VIVE_DEBUG(d, "model_number: %s", d->firmware.model_number);
		VIVE_DEBUG(d, "mb_serial_number: %s",
		           d->firmware.mb_serial_number);
		VIVE_DEBUG(d, "device_serial_number: %s",
		           d->firmware.device_serial_number);

		VIVE_DEBUG(d, "eye_target_height_in_pixels: %d",
		           d->display.eye_target_height_in_pixels);
		VIVE_DEBUG(d, "eye_target_width_in_pixels: %d",
		           d->display.eye_target_width_in_pixels);

		if (d->print_debug) {
			print_vec3("acc_bias", &d->imu.acc_bias);
			print_vec3("acc_scale", &d->imu.acc_scale);
			print_vec3("gyro_bias", &d->imu.gyro_bias);
			print_vec3("gyro_scale", &d->imu.gyro_scale);
		}

		VIVE_DEBUG(
		    d, "grow_for_undistort: %f",
		    (double)d->base.hmd->distortion.vive.grow_for_undistort);

		VIVE_DEBUG(d, "undistort_r2_cutoff 0: %f",
		           (double)d->base.hmd->distortion.vive
		               .undistort_r2_cutoff[0]);
		VIVE_DEBUG(d, "undistort_r2_cutoff 1: %f",
		           (double)d->base.hmd->distortion.vive
		               .undistort_r2_cutoff[1]);
	} else {
		VIVE_ERROR("Could not parse JSON data.");
		return false;
	}

	return true;
}

char *
vive_sensors_read_config(struct vive_device *d)
{
	struct vive_config_start_report start_report = {
	    .id = VIVE_CONFIG_START_REPORT_ID,
	};

	unsigned char *config_json;
	unsigned char *config_z;
	uint32_t count = 0;
	int ret;

	ret = os_hid_get_feature_timeout(d->sensors_dev, &start_report,
	                                 sizeof(start_report), 100);
	if (ret < 0) {
		VIVE_ERROR("Could not get config start report.");
		return NULL;
	}

	struct vive_config_read_report report = {
	    .id = VIVE_CONFIG_READ_REPORT_ID,
	};

	config_z = malloc(4096);

	do {
		ret = os_hid_get_feature_timeout(d->sensors_dev, &report,
		                                 sizeof(report), 100);
		if (ret < 0) {
			VIVE_ERROR("Read error after %d bytes: %d", count, ret);
			free(config_z);
			return NULL;
		}

		if (report.len > 62) {
			VIVE_ERROR("Invalid configuration data at %d", count);
			free(config_z);
			return NULL;
		}

		if (count + report.len > 4096) {
			VIVE_ERROR("Configuration data too large");
			free(config_z);
			return NULL;
		}

		memcpy(config_z + count, report.payload, report.len);
		count += report.len;
	} while (report.len);

	config_json = malloc(32768);

	z_stream strm = {.zalloc = Z_NULL,
	                 .zfree = Z_NULL,
	                 .opaque = Z_NULL,
	                 .avail_in = count,
	                 .next_in = config_z,
	                 .avail_out = 32768,
	                 .next_out = config_json};

	ret = inflateInit(&strm);
	if (ret != Z_OK) {
		VIVE_ERROR("inflate_init failed: %d", ret);
		free(config_z);
		return NULL;
	}

	ret = inflate(&strm, Z_FINISH);
	free(config_z);
	if (ret != Z_STREAM_END) {
		VIVE_ERROR("Failed to inflate configuration data: %d", ret);
		free(config_json);
		return NULL;
	}

	config_json[strm.total_out] = '\0';

	return realloc(config_json, strm.total_out + 1);
}

struct vive_device *
vive_device_create(struct os_hid_device *mainboard_dev,
                   struct os_hid_device *sensors_dev,
                   enum VIVE_VARIANT variant)
{
	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)(
	    U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct vive_device *d =
	    U_DEVICE_ALLOCATE(struct vive_device, flags, 1, 0);

	d->base.hmd->blend_mode = XRT_BLEND_MODE_OPAQUE;
	d->base.update_inputs = vive_device_update_inputs;
	d->base.get_tracked_pose = vive_device_get_tracked_pose;
	d->base.get_view_pose = vive_device_get_view_pose;
	d->base.destroy = vive_device_destroy;
	d->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_RELATION;
	d->base.name = XRT_DEVICE_GENERIC_HMD;
	d->mainboard_dev = mainboard_dev;
	d->sensors_dev = sensors_dev;
	d->print_spew = debug_get_bool_option_vive_spew();
	d->print_debug = debug_get_bool_option_vive_debug();
	d->rot_filtered.w = 1.0f;
	d->variant = variant;

	snprintf(d->base.str, XRT_DEVICE_NAME_LEN, "Vive-family Device");

	vive_mainboard_power_on(d);
	vive_mainboard_get_device_info(d);
	vive_sensors_read_firmware(d);
	vive_sensros_get_imu_range_report(d);

	char *config = vive_sensors_read_config(d);
	vive_parse_config(d, config);
	free(config);

	// TODO: Replace hard coded values from OpenHMD with config
	double w_meters = 0.122822 / 2.0;
	double h_meters = 0.068234;
	double lens_horizontal_separation = 0.057863;
	double eye_to_screen_distance = 0.023226876441867737;
	double fov = 2 * atan2(w_meters - lens_horizontal_separation / 2.0,
	                       eye_to_screen_distance);

	uint32_t w_pixels = d->display.eye_target_width_in_pixels;
	uint32_t h_pixels = d->display.eye_target_height_in_pixels;

	// Main display.
	d->base.hmd->screens[0].w_pixels = (int)w_pixels * 2;
	d->base.hmd->screens[0].h_pixels = (int)h_pixels;
	d->base.hmd->screens[0].nominal_frame_interval_ns =
	    (uint64_t)time_s_to_ns(1.0f / 90.0f);

	for (uint8_t eye = 0; eye < 2; eye++) {
		struct xrt_view *v = &d->base.hmd->views[eye];
		v->display.w_meters = (float)w_meters;
		v->display.h_meters = (float)h_meters;
		v->display.w_pixels = w_pixels;
		v->display.h_pixels = h_pixels;
		v->viewport.w_pixels = w_pixels;
		v->viewport.h_pixels = h_pixels;
		v->viewport.y_pixels = 0;
		v->lens_center.y_meters = (float)h_meters / 2.0f;
		v->rot = u_device_rotation_ident;
	}

	// Left
	d->base.hmd->views[0].lens_center.x_meters =
	    (float)(w_meters - lens_horizontal_separation / 2.0);
	d->base.hmd->views[0].viewport.x_pixels = 0;

	// Right
	d->base.hmd->views[1].lens_center.x_meters =
	    (float)lens_horizontal_separation / 2.0f;
	d->base.hmd->views[1].viewport.x_pixels = w_pixels;

	for (uint8_t eye = 0; eye < 2; eye++) {
		if (!math_compute_fovs(
		        w_meters,
		        (double)d->base.hmd->views[eye].lens_center.x_meters,
		        fov, h_meters,
		        (double)d->base.hmd->views[eye].lens_center.y_meters, 0,
		        &d->base.hmd->views[eye].fov)) {
			VIVE_ERROR(
			    "Failed to compute the partial fields of view.");
			free(d);
			return NULL;
		}
	}

	d->base.hmd->distortion.models = XRT_DISTORTION_MODEL_VIVE;
	d->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_VIVE;

	int ret;
	ret = os_thread_helper_start(&d->mainboard_thread,
	                             vive_mainboard_run_thread, d);
	if (ret != 0) {
		VIVE_ERROR("Failed to start mainboard thread!");
		vive_device_destroy((struct xrt_device *)d);
		return NULL;
	}

	ret = os_thread_helper_start(&d->sensors_thread,
	                             vive_sensors_run_thread, d);
	if (ret != 0) {
		VIVE_ERROR("Failed to start sensors thread!");
		vive_device_destroy((struct xrt_device *)d);
		return NULL;
	}

	return d;
}
