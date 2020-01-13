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
#include "util/u_json.h"
#include "util/u_var.h"

#include "math/m_api.h"

#include "os/os_hid.h"


#include "vive_device.h"
#include "vive_protocol.h"

#define VIVE_CLOCK_FREQ 48e6 // 48 MHz

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
	if (d->mainboard_dev)
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

	if (d->lh.sensors != NULL) {
		free(d->lh.sensors);
		d->lh.sensors = NULL;
		d->lh.num_sensors = 0;
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
	VIVE_SPEW(d, "ENTER!");
}

static void
vive_device_get_tracked_pose(struct xrt_device *xdev,
                             enum xrt_input_name name,
                             struct time_state *timekeeping,
                             int64_t *out_timestamp,
                             struct xrt_space_relation *out_relation)
{
	struct vive_device *d = vive_device(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
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
	struct vive_device *d = vive_device(xdev);
	struct xrt_pose pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
	bool adjust = view_index == 0;

	pose.orientation = d->display.rot[view_index];
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
		d->rot_filtered = (struct xrt_quat){0, 0, 0, 1};
	}
}

static inline int
oldest_sequence_index(uint8_t a, uint8_t b, uint8_t c)
{
	if (a == (uint8_t)(b + 2))
		return 1;
	if (b == (uint8_t)(c + 2))
		return 2;

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

		int16_t acc[3] = {
		    (int16_t)__le16_to_cpu(sample->acc[0]),
		    (int16_t)__le16_to_cpu(sample->acc[1]),
		    (int16_t)__le16_to_cpu(sample->acc[2]),
		};

		scale = (float)d->imu.acc_range / 32768.0f;
		struct xrt_vec3 acceleration = {
		    scale * d->imu.acc_scale.x * acc[0] - d->imu.acc_bias.x,
		    scale * d->imu.acc_scale.y * acc[1] - d->imu.acc_bias.y,
		    scale * d->imu.acc_scale.z * acc[2] - d->imu.acc_bias.z,
		};

		int16_t gyro[3] = {
		    (int16_t)__le16_to_cpu(sample->gyro[0]),
		    (int16_t)__le16_to_cpu(sample->gyro[1]),
		    (int16_t)__le16_to_cpu(sample->gyro[2]),
		};

		scale = (float)d->imu.gyro_range / 32768.0f;
		struct xrt_vec3 angular_velocity = {
		    scale * d->imu.gyro_scale.x * gyro[0] - d->imu.gyro_bias.x,
		    scale * d->imu.gyro_scale.y * gyro[1] - d->imu.gyro_bias.y,
		    scale * d->imu.gyro_scale.z * gyro[2] - d->imu.gyro_bias.z,
		};

		VIVE_SPEW(d, "ACC  %f %f %f", acceleration.x, acceleration.y,
		          acceleration.z);

		VIVE_SPEW(d, "GYRO %f %f %f", angular_velocity.x,
		          angular_velocity.y, angular_velocity.z);

		switch (d->variant) {
		case VIVE_VARIANT_VIVE:
			// flip all except x axis
			angular_velocity.x = +angular_velocity.x;
			angular_velocity.y = -angular_velocity.y;
			angular_velocity.z = -angular_velocity.z;
			break;
		case VIVE_VARIANT_PRO:
			// flip all except y axis
			angular_velocity.x = -angular_velocity.x;
			angular_velocity.y = +angular_velocity.y;
			angular_velocity.z = -angular_velocity.z;
			break;
		case VIVE_VARIANT_INDEX: {
			// Flip all axis and re-order.
			struct xrt_vec3 angular_velocity_fixed;
			angular_velocity_fixed.x = -angular_velocity.y;
			angular_velocity_fixed.y = -angular_velocity.x;
			angular_velocity_fixed.z = -angular_velocity.z;
			angular_velocity = angular_velocity_fixed;
		} break;
		default: VIVE_ERROR("Unhandled Vive variant\n"); return;
		}

		math_quat_integrate_velocity(
		    &d->rot_filtered, &angular_velocity,
		    (float)(dt / VIVE_CLOCK_FREQ), &d->rot_filtered);

		d->last.acc = acceleration;
		d->last.gyro = angular_velocity;
		d->imu.sequence = seq;
		d->imu.time = raw_time;
	}
}


/*
 *
 * Mainboard thread
 *
 */

static bool
vive_mainboard_read_one_msg(struct vive_device *d)
{
	uint8_t buffer[64];

	int ret = os_hid_read(d->mainboard_dev, buffer, sizeof(buffer), 1000);
	if (ret == 0) {
		// Time out
		return true;
	}
	if (ret < 0) {
		VIVE_ERROR("Failed to read device '%i'!", ret);
		return false;
	}

	switch (buffer[0]) {
	case VIVE_MAINBOARD_STATUS_REPORT_ID:
		if (ret != sizeof(struct vive_mainboard_status_report)) {
			VIVE_ERROR("Mainboard status report has invalid size.");
			return false;
		}
		vive_mainboard_decode_message(
		    d, (struct vive_mainboard_status_report *)buffer);
		break;
	default:
		VIVE_ERROR("Unknown mainboard message type %d", buffer[0]);
		break;
	}

	return true;
}

static void *
vive_mainboard_run_thread(void *ptr)
{
	struct vive_device *d = (struct vive_device *)ptr;

	os_thread_helper_lock(&d->mainboard_thread);
	while (os_thread_helper_is_running_locked(&d->mainboard_thread)) {
		os_thread_helper_unlock(&d->mainboard_thread);

		if (!vive_mainboard_read_one_msg(d)) {
			return NULL;
		}

		// Just keep swimming.
		os_thread_helper_lock(&d->mainboard_thread);
	}

	return NULL;
}


/*
 *
 * Sensor thread.
 *
 */

static bool
vive_sensors_read_one_msg(struct vive_device *d)
{
	uint8_t buffer[64];

	int ret = os_hid_read(d->sensors_dev, buffer, sizeof(buffer), 1000);
	if (ret == 0) {
		// Time out
		return true;
	}
	if (ret < 0) {
		VIVE_ERROR("Failed to read device '%i'!", ret);
		return false;
	}

	switch (buffer[0]) {
	case VIVE_IMU_REPORT_ID:
		if (ret != 52) {
			VIVE_ERROR("Wrong IMU report size: %d", ret);
			return false;
		}
		update_imu(d, (struct vive_imu_report *)buffer);
		break;
	default: VIVE_ERROR("Unknown sensor message type %d", buffer[0]); break;
	}

	return true;
}

static void *
vive_sensors_run_thread(void *ptr)
{
	struct vive_device *d = (struct vive_device *)ptr;

	os_thread_helper_lock(&d->sensors_thread);
	while (os_thread_helper_is_running_locked(&d->sensors_thread)) {
		os_thread_helper_unlock(&d->sensors_thread);

		if (!vive_sensors_read_one_msg(d)) {
			return NULL;
		}

		// Just keep swimming.
		os_thread_helper_lock(&d->sensors_thread);
	}

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
vive_sensors_get_imu_range_report(struct vive_device *d)
{
	struct vive_imu_range_modes_report report = {
	    .id = VIVE_IMU_RANGE_MODES_REPORT_ID};

	int ret;
	ret = os_hid_get_feature(d->sensors_dev, report.id, (uint8_t *)&report,
	                         sizeof(report));
	if (ret < 0) {
		printf("Could not get range report!\n");
		return ret;
	}

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
		return -1;
	}

	/*
	 * Convert MPU-6500 gyro full scale range (+/-250°/s, +/-500°/s,
	 * +/-1000°/s, or +/-2000°/s) into rad/s, accel full scale range
	 * (+/-2g, +/-4g, +/-8g, or +/-16g) into m/s².
	 */

	d->imu.gyro_range = M_PI / 180.0 * (250 << report.gyro_range);
	VIVE_DEBUG(d, "Vive gyroscope range     %f", d->imu.gyro_range);

	d->imu.acc_range = MATH_GRAVITY_M_S2 * (2 << report.accel_range);
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
_array_to_vec3(const float array[3], struct xrt_vec3 *result)
{
	result->x = array[0];
	result->y = array[1];
	result->z = array[2];
}

static void
_json_to_vec3(const cJSON *json, struct xrt_vec3 *result)
{
	float result_array[3];

	assert(cJSON_GetArraySize(json) == 3);
	const cJSON *item = NULL;
	size_t i = 0;
	cJSON_ArrayForEach(item, json)
	{
		assert(cJSON_IsNumber(item));
		result_array[i] = (float)item->valuedouble;
		++i;
		if (i == 3) {
			break;
		}
	}

	_array_to_vec3(result_array, result);
}

static long long
_json_to_int(const cJSON *item)
{
	if (item != NULL) {
		return item->valueint;
	} else {
		return 0;
	}
}

static void
_json_get_vec3(const cJSON *json, const char *name, struct xrt_vec3 *result)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);

	_json_to_vec3(item, result);
}

static bool
_json_get_matrix_3x3(const cJSON *json,
                     const char *name,
                     struct xrt_matrix_3x3 *result)
{
	const cJSON *vec3_arr = cJSON_GetObjectItemCaseSensitive(json, name);

	// Some sanity checking.
	if (vec3_arr == NULL || cJSON_GetArraySize(vec3_arr) != 3) {
		return false;
	}

	size_t total = 0;
	const cJSON *vec = NULL;
	cJSON_ArrayForEach(vec, vec3_arr)
	{
		assert(cJSON_GetArraySize(vec) == 3);
		const cJSON *elem = NULL;
		cJSON_ArrayForEach(elem, vec)
		{
			// Just in case.
			if (total >= 9) {
				break;
			}

			assert(cJSON_IsNumber(elem));
			result->v[total++] = (float)elem->valuedouble;
		}
	}

	return true;
}

static char *
_json_get_string(const cJSON *json, const char *name)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	return strdup(item->string);
}

static double
_json_get_double(const cJSON *json, const char *name)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	return item->valuedouble;
}

static float
_json_get_float(const cJSON *json, const char *name)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	return (float)item->valuedouble;
}

static long long
_json_get_int(const cJSON *json, const char *name)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	return _json_to_int(item);
}

static void
_get_color_coeffs(struct xrt_hmd_parts *hmd,
                  const cJSON *coeffs,
                  uint8_t eye,
                  uint8_t channel)
{
	// this is 4 on index, all values populated
	// assert(coeffs->length == 8);
	// only 3 coeffs contain values
	const cJSON *item = NULL;
	size_t i = 0;
	cJSON_ArrayForEach(item, coeffs)
	{
		hmd->distortion.vive.coefficients[eye][i][channel] =
		    (float)item->valuedouble;
		++i;
		if (i == 3) {
			break;
		}
	}
}

static void
_get_color_coeffs_lookup(struct xrt_hmd_parts *hmd,
                         const cJSON *eye_json,
                         const char *name,
                         uint8_t eye,
                         uint8_t channel)
{
	const cJSON *distortion =
	    cJSON_GetObjectItemCaseSensitive(eye_json, name);
	if (distortion == NULL) {
		return;
	}

	const cJSON *coeffs =
	    cJSON_GetObjectItemCaseSensitive(distortion, "coeffs");
	if (coeffs == NULL) {
		return;
	}

	_get_color_coeffs(hmd, coeffs, eye, channel);
}

static void
_get_pose_from_pos_x_z(const cJSON *obj, struct xrt_pose *pose)
{
	struct xrt_vec3 plus_x, plus_z;
	_json_get_vec3(obj, "plus_x", &plus_x);
	_json_get_vec3(obj, "plus_z", &plus_z);
	_json_get_vec3(obj, "position", &pose->position);

	math_quat_from_plus_x_z(&plus_x, &plus_z, &pose->orientation);
}

static void
get_distortion_properties(struct vive_device *d,
                          const cJSON *eye_transform_json,
                          uint8_t eye)
{
	struct xrt_hmd_parts *hmd = d->base.hmd;

	const cJSON *eye_json = cJSON_GetArrayItem(eye_transform_json, eye);
	if (eye_json == NULL) {
		return;
	}

	struct xrt_matrix_3x3 rot = {0};
	if (_json_get_matrix_3x3(eye_json, "eye_to_head", &rot)) {
		math_quat_from_matrix_3x3(&rot, &d->display.rot[eye]);
	}

	// TODO: store grow_for_undistort per eye
	// clang-format off
	hmd->distortion.vive.grow_for_undistort = _json_get_float(eye_json, "grow_for_undistort");
	hmd->distortion.vive.undistort_r2_cutoff[eye] = _json_get_float(eye_json, "undistort_r2_cutoff");
	// clang-format on

	const cJSON *distortion =
	    cJSON_GetObjectItemCaseSensitive(eye_json, "distortion");
	if (distortion != NULL) {
		// TODO: store center per color
		// clang-format off
		hmd->distortion.vive.center[eye][0] = _json_get_float(distortion, "center_x");
		hmd->distortion.vive.center[eye][1] = _json_get_float(distortion, "center_y");
		// clang-format on

		// green
		const cJSON *coeffs =
		    cJSON_GetObjectItemCaseSensitive(distortion, "coeffs");
		if (coeffs != NULL) {
			_get_color_coeffs(hmd, coeffs, eye, 1);
		}
	}

	_get_color_coeffs_lookup(hmd, eye_json, "distortion_red", eye, 0);
	_get_color_coeffs_lookup(hmd, eye_json, "distortion_blue", eye, 2);
}

static void
get_lighthouse_config(struct vive_device *d, const cJSON *json)
{
	const cJSON *lh =
	    cJSON_GetObjectItemCaseSensitive(json, "lighthouse_config");
	if (lh == NULL) {
		return;
	}

	const cJSON *json_map =
	    cJSON_GetObjectItemCaseSensitive(lh, "channelMap");
	const cJSON *json_normals =
	    cJSON_GetObjectItemCaseSensitive(lh, "modelNormals");
	const cJSON *json_points =
	    cJSON_GetObjectItemCaseSensitive(lh, "modelPoints");

	if (json_map == NULL || json_normals == NULL || json_points == NULL) {
		return;
	}

	size_t map_size = cJSON_GetArraySize(json_map);
	size_t normals_size = cJSON_GetArraySize(json_normals);
	size_t points_size = cJSON_GetArraySize(json_points);

	if (map_size != normals_size || normals_size != points_size ||
	    map_size <= 0) {
		return;
	}

	uint32_t *map = U_TYPED_ARRAY_CALLOC(uint32_t, map_size);
	struct lh_sensor *s = U_TYPED_ARRAY_CALLOC(struct lh_sensor, map_size);

	size_t i = 0;
	const cJSON *item = NULL;
	cJSON_ArrayForEach(item, json_map)
	{
		// Build the channel map.
		map[i++] = _json_to_int(item);
	}

	i = 0;
	item = NULL;
	cJSON_ArrayForEach(item, json_normals)
	{
		// Store in channel map order.
		_json_to_vec3(item, &s[map[i++]].normal);
	}

	i = 0;
	item = NULL;
	cJSON_ArrayForEach(item, json_points)
	{
		// Store in channel map order.
		_json_to_vec3(item, &s[map[i++]].pos);
	}

	// Free the map.
	free(map);
	map = NULL;

	d->lh.sensors = s;
	d->lh.num_sensors = map_size;


	// Transform the sensors into IMU space.
	struct xrt_pose trackref_to_imu = {0};
	math_pose_invert(&d->imu.trackref, &trackref_to_imu);

	for (i = 0; i < d->lh.num_sensors; i++) {
		struct xrt_vec3 point = d->lh.sensors[i].pos;
		struct xrt_vec3 normal = d->lh.sensors[i].normal;

		math_quat_rotate_vec3(&trackref_to_imu.orientation, &normal,
		                      &d->lh.sensors[i].normal);
		math_pose_transform_point(&trackref_to_imu, &point,
		                          &d->lh.sensors[i].pos);
	}
}

void
vive_init_defaults(struct vive_device *d)
{
	d->display.eye_target_width_in_pixels = 1080;
	d->display.eye_target_height_in_pixels = 1200;

	d->display.rot[0].w = 1.0f;
	d->display.rot[1].w = 1.0f;

	d->imu.gyro_range = 8.726646f;
	d->imu.acc_range = 39.226600f;

	d->imu.acc_scale.x = 1.0f;
	d->imu.acc_scale.y = 1.0f;
	d->imu.acc_scale.z = 1.0f;

	d->imu.gyro_scale.x = 1.0f;
	d->imu.gyro_scale.y = 1.0f;
	d->imu.gyro_scale.z = 1.0f;

	d->rot_filtered.w = 1.0f;

	struct xrt_hmd_parts *hmd = d->base.hmd;
	hmd->distortion.vive.aspect_x_over_y = 0.89999997615814209f;
	hmd->distortion.vive.grow_for_undistort = 0.5f;
	hmd->distortion.vive.undistort_r2_cutoff[0] = 1.0f;
	hmd->distortion.vive.undistort_r2_cutoff[1] = 1.0f;
}

bool
vive_parse_config(struct vive_device *d, char *json_string)
{
	VIVE_DEBUG(d, "JSON config:\n%s\n", json_string);

	cJSON *json = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json)) {
		VIVE_ERROR("Could not parse JSON data.");
		return false;
	}

	switch (d->variant) {
	case VIVE_VARIANT_VIVE:
		_json_get_vec3(json, "acc_bias", &d->imu.acc_bias);
		_json_get_vec3(json, "acc_scale", &d->imu.acc_scale);
		_json_get_vec3(json, "gyro_bias", &d->imu.gyro_bias);
		_json_get_vec3(json, "gyro_scale", &d->imu.gyro_scale);
		break;
	case VIVE_VARIANT_PRO: {
		const cJSON *imu =
		    cJSON_GetObjectItemCaseSensitive(json, "imu");
		_json_get_vec3(imu, "acc_bias", &d->imu.acc_bias);
		_json_get_vec3(imu, "acc_scale", &d->imu.acc_scale);
		_json_get_vec3(imu, "gyro_bias", &d->imu.gyro_bias);
		_json_get_vec3(imu, "gyro_scale", &d->imu.gyro_scale);
	} break;
	case VIVE_VARIANT_INDEX: {
		const cJSON *head =
		    cJSON_GetObjectItemCaseSensitive(json, "head");
		_get_pose_from_pos_x_z(head, &d->display.trackref);

		const cJSON *imu =
		    cJSON_GetObjectItemCaseSensitive(json, "imu");
		_get_pose_from_pos_x_z(imu, &d->imu.trackref);

		_json_get_vec3(imu, "acc_bias", &d->imu.acc_bias);
		_json_get_vec3(imu, "acc_scale", &d->imu.acc_scale);
		_json_get_vec3(imu, "gyro_bias", &d->imu.gyro_bias);

		get_lighthouse_config(d, json);

		struct xrt_pose trackref_to_head;
		struct xrt_pose imu_to_head;

		math_pose_invert(&d->display.trackref, &trackref_to_head);
		math_pose_transform(&trackref_to_head, &d->imu.trackref,
		                    &imu_to_head);

		d->display.imuref = imu_to_head;
	} break;
	default: VIVE_ERROR("Unknown Vive variant.\n"); return false;
	}

	d->firmware.model_number = _json_get_string(json, "model_number");
	if (d->variant != VIVE_VARIANT_INDEX) {
		// clang-format off
		d->firmware.mb_serial_number = _json_get_string(json, "mb_serial_number");
		d->display.lens_separation = _json_get_double(json, "lens_separation");
		// clang-format on
	}
	d->firmware.device_serial_number =
	    _json_get_string(json, "device_serial_number");

	const cJSON *device_json =
	    cJSON_GetObjectItemCaseSensitive(json, "device");
	if (device_json) {
		if (d->variant != VIVE_VARIANT_INDEX) {
			d->display.persistence =
			    _json_get_double(device_json, "persistence");
			d->base.hmd->distortion.vive.aspect_x_over_y =
			    _json_get_float(device_json,
			                    "physical_aspect_x_over_y");
		}
		d->display.eye_target_height_in_pixels =
		    (uint16_t)_json_get_int(device_json,
		                            "eye_target_height_in_pixels");
		d->display.eye_target_width_in_pixels = (uint16_t)_json_get_int(
		    device_json, "eye_target_width_in_pixels");
	}

	const cJSON *eye_transform_json =
	    cJSON_GetObjectItemCaseSensitive(json, "tracking_to_eye_transform");
	if (eye_transform_json) {
		for (uint8_t eye = 0; eye < 2; eye++) {
			get_distortion_properties(d, eye_transform_json, eye);
		}
	}

	cJSON_Delete(json);

	// clang-format off
	VIVE_DEBUG(d, "= Vive configuration =");
	VIVE_DEBUG(d, "lens_separation: %f", d->display.lens_separation);
	VIVE_DEBUG(d, "persistence: %f", d->display.persistence);
	VIVE_DEBUG(d, "physical_aspect_x_over_y: %f", (double)d->base.hmd->distortion.vive.aspect_x_over_y);

	VIVE_DEBUG(d, "model_number: %s", d->firmware.model_number);
	VIVE_DEBUG(d, "mb_serial_number: %s", d->firmware.mb_serial_number);
	VIVE_DEBUG(d, "device_serial_number: %s", d->firmware.device_serial_number);

	VIVE_DEBUG(d, "eye_target_height_in_pixels: %d", d->display.eye_target_height_in_pixels);
	VIVE_DEBUG(d, "eye_target_width_in_pixels: %d", d->display.eye_target_width_in_pixels);

	if (d->print_debug) {
		print_vec3("acc_bias", &d->imu.acc_bias);
		print_vec3("acc_scale", &d->imu.acc_scale);
		print_vec3("gyro_bias", &d->imu.gyro_bias);
		print_vec3("gyro_scale", &d->imu.gyro_scale);
	}

	VIVE_DEBUG(d, "grow_for_undistort: %f", (double)d->base.hmd->distortion.vive.grow_for_undistort);

	VIVE_DEBUG(d, "undistort_r2_cutoff 0: %f", (double)d->base.hmd->distortion.vive.undistort_r2_cutoff[0]);
	VIVE_DEBUG(d, "undistort_r2_cutoff 1: %f", (double)d->base.hmd->distortion.vive.undistort_r2_cutoff[1]);
	// clang-format on

	return true;
}

char *
vive_sensors_read_config(struct vive_device *d)
{
	struct vive_config_start_report start_report = {
	    .id = VIVE_CONFIG_START_REPORT_ID,
	};

	int ret = os_hid_get_feature_timeout(d->sensors_dev, &start_report,
	                                     sizeof(start_report), 100);
	if (ret < 0) {
		VIVE_ERROR("Could not get config start report.");
		return NULL;
	}

	struct vive_config_read_report report = {
	    .id = VIVE_CONFIG_READ_REPORT_ID,
	};

	unsigned char *config_z = U_TYPED_ARRAY_CALLOC(unsigned char, 4096);

	uint32_t count = 0;
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
		VIVE_ERROR("inflate_init failed: %d", ret);
		free(config_z);
		free(config_json);
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

	U_ARRAY_REALLOC_OR_FREE(config_json, unsigned char, strm.total_out + 1);
	return (char *)config_json;
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
	d->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	d->base.name = XRT_DEVICE_GENERIC_HMD;
	d->mainboard_dev = mainboard_dev;
	d->sensors_dev = sensors_dev;
	d->print_spew = debug_get_bool_option_vive_spew();
	d->print_debug = debug_get_bool_option_vive_debug();
	d->variant = variant;

	vive_init_defaults(d);

	switch (variant) {
	case VIVE_VARIANT_VIVE:
		snprintf(d->base.str, XRT_DEVICE_NAME_LEN, "HTC Vive");
		break;
	case VIVE_VARIANT_PRO:
		snprintf(d->base.str, XRT_DEVICE_NAME_LEN, "HTC Vive Pro");
		break;
	case VIVE_VARIANT_INDEX:
		snprintf(d->base.str, XRT_DEVICE_NAME_LEN, "Valve Index");
		break;
	default:
		snprintf(d->base.str, XRT_DEVICE_NAME_LEN,
		         "Unknown Vive device");
	}

	if (d->mainboard_dev) {
		vive_mainboard_power_on(d);
		vive_mainboard_get_device_info(d);
	}
	vive_sensors_read_firmware(d);

	vive_sensors_get_imu_range_report(d);

	char *config = vive_sensors_read_config(d);
	if (config != NULL) {
		vive_parse_config(d, config);
		free(config);
	}

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

	if (d->variant == VIVE_VARIANT_INDEX)
		d->base.hmd->screens[0].nominal_frame_interval_ns =
		    (uint64_t)time_s_to_ns(1.0f / 144.0f);
	else
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

	u_var_add_root(d, "Vive Device", true);
	u_var_add_gui_header(d, &d->gui.calibration, "Calibration");
	u_var_add_vec3_f32(d, &d->imu.acc_scale, "acc_scale");
	u_var_add_vec3_f32(d, &d->imu.acc_bias, "acc_bias");
	u_var_add_vec3_f32(d, &d->imu.gyro_scale, "gyro_scale");
	u_var_add_vec3_f32(d, &d->imu.gyro_bias, "gyro_bias");
	u_var_add_gui_header(d, &d->gui.last, "Last data");
	u_var_add_vec3_f32(d, &d->last.acc, "acc");
	u_var_add_vec3_f32(d, &d->last.gyro, "gyro");

	int ret;

	if (d->mainboard_dev) {
		ret = os_thread_helper_start(&d->mainboard_thread,
		                             vive_mainboard_run_thread, d);
		if (ret != 0) {
			VIVE_ERROR("Failed to start mainboard thread!");
			vive_device_destroy((struct xrt_device *)d);
			return NULL;
		}
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
