// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Arduino felxable input device code.
 * @author Pete Black <pete.black@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_arduino
 */

#include "xrt/xrt_device.h"
#include "xrt/xrt_prober.h"
#include "xrt/xrt_tracking.h"

#include "os/os_ble.h"
#include "os/os_time.h"
#include "os/os_threading.h"

#include "math/m_api.h"
#include "math/m_imu_pre.h"
#include "math/m_imu_3dof.h"

#include "util/u_var.h"
#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_bitwise.h"
#include "util/u_logging.h"

#include "arduino_interface.h"

#include <stdio.h>
#include <math.h>
#include <assert.h>


DEBUG_GET_ONCE_LOG_OPTION(arduino_log, "ARDUINO_LOG", U_LOGGING_WARN)

/*
 *
 * Structs.
 *
 */

/*!
 * A parsed sample of accel and gyro.
 */
struct arduino_parsed_sample
{
	uint32_t time;
	uint32_t delta;
	struct xrt_vec3_i32 accel;
	struct xrt_vec3_i32 gyro;
};

struct arduino_parsed_input
{
	uint32_t timestamp;
	struct arduino_parsed_sample sample;
};

/*!
 * @implements xrt_device
 */
struct arduino_device
{
	struct xrt_device base;
	struct os_ble_device *ble;
	struct os_thread_helper oth;



	struct
	{

		//! Device time.
		uint64_t device_time;

		//! Lock for last and fusion.
		struct os_mutex lock;

		uint64_t last_time;

		//! Pre filter for the IMU.
		struct m_imu_pre_filter pre_filter;

		struct m_imu_3dof fusion;
	};


	struct
	{
		bool last;
	} gui;

	enum u_logging_level ll;
};


/*
 *
 * Smaller helper functions.
 *
 */

#define ARDUINO_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->ll, __VA_ARGS__)
#define ARDUINO_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->ll, __VA_ARGS__)
#define ARDUINO_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->ll, __VA_ARGS__)
#define ARDUINO_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->ll, __VA_ARGS__)
#define ARDUINO_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->ll, __VA_ARGS__)

static inline struct arduino_device *
arduino_device(struct xrt_device *xdev)
{
	return (struct arduino_device *)xdev;
}

static uint32_t
calc_delta_and_handle_rollover(uint32_t next, uint32_t last)
{
	uint32_t tick_delta = next - last;

	// The 24-bit tick counter has rolled over,
	// adjust the "negative" value to be positive.
	if (tick_delta > 0xffffff) {
		tick_delta += 0x1000000;
	}

	return tick_delta;
}

static int16_t
read_i16(const uint8_t *buffer, size_t offset)
{
	return (buffer[offset] << 8) | buffer[offset + 1];
}


/*
 *
 * Internal functions.
 *
 */

static void
update_fusion(struct arduino_device *ad,
              struct arduino_parsed_sample *sample,
              timepoint_ns timestamp_ns,
              time_duration_ns delta_ns)
{
	struct xrt_vec3 accel, gyro;
	m_imu_pre_filter_data(&ad->pre_filter, &sample->accel, &sample->gyro, &accel, &gyro);

	ad->device_time += (uint64_t)sample->delta * 1000;

	m_imu_3dof_update(&ad->fusion, ad->device_time, &accel, &gyro);

	double delta_device_ms = (double)sample->delta / 1000.0;
	double delta_host_ms = (double)delta_ns / (1000.0 * 1000.0);
	ARDUINO_DEBUG(ad, "%+fms %+fms", delta_host_ms, delta_device_ms);
	ARDUINO_DEBUG(ad, "fusion sample %u (ax %d ay %d az %d) (gx %d gy %d gz %d)", sample->time, sample->accel.x,
	              sample->accel.y, sample->accel.z, sample->gyro.x, sample->gyro.y, sample->gyro.z);
	ARDUINO_DEBUG(ad, " ");
}

static void
arduino_parse_input(struct arduino_device *ad, void *data, struct arduino_parsed_input *input)
{
	U_ZERO(input);
	unsigned char *b = (unsigned char *)data;
	ARDUINO_TRACE(ad,
	              "raw input: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
	              "%02x %02x %02x %02x %02x %02x %02x %02x %02x",
	              b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14],
	              b[15], b[16], b[17], b[18], b[19]);

	uint32_t time = b[5] | b[4] << 8 | b[3] << 16;

	input->sample.time = time;
	input->sample.delta = calc_delta_and_handle_rollover(time, ad->last_time);
	ad->last_time = time;

	input->sample.accel.x = read_i16(b, 6);
	input->sample.accel.y = read_i16(b, 8);
	input->sample.accel.z = read_i16(b, 10);
	input->sample.gyro.x = read_i16(b, 12);
	input->sample.gyro.y = read_i16(b, 14);
	input->sample.gyro.z = read_i16(b, 16);
}

/*!
 * Reads one packet from the device,handles locking and checking if
 * the thread has been told to shut down.
 */
static bool
arduino_read_one_packet(struct arduino_device *ad, uint8_t *buffer, size_t size)
{
	os_thread_helper_lock(&ad->oth);

	while (os_thread_helper_is_running_locked(&ad->oth)) {
		int retries = 5;
		int ret = -1;
		os_thread_helper_unlock(&ad->oth);

		while (retries > 0) {
			ret = os_ble_read(ad->ble, buffer, size, 500);
			if (ret == (int)size) {
				break;
			}
			retries--;
		}
		if (ret == 0) {
			ARDUINO_ERROR(ad, "%s", __func__);
			// Must lock thread before check in while.
			os_thread_helper_lock(&ad->oth);
			continue;
		}
		if (ret < 0) {
			ARDUINO_ERROR(ad, "Failed to read device '%i'!", ret);
			return false;
		}
		return true;
	}

	return false;
}

static void *
arduino_run_thread(void *ptr)
{
	struct arduino_device *ad = (struct arduino_device *)ptr;
	uint8_t buffer[20];
	timepoint_ns then_ns, now_ns;
	struct arduino_parsed_input input; // = {0};

	// wait for a package to sync up, it's discarded but that's okay.
	if (!arduino_read_one_packet(ad, buffer, 20)) {
		return NULL;
	}

	then_ns = os_monotonic_get_ns();
	while (arduino_read_one_packet(ad, buffer, 20)) {

		// As close to when we get a packet.
		now_ns = os_monotonic_get_ns();

		// Parse the data we got.
		arduino_parse_input(ad, buffer, &input);

		time_duration_ns delta_ns = now_ns - then_ns;
		then_ns = now_ns;

		// Lock last and the fusion.
		os_mutex_lock(&ad->lock);

		// Process the parsed data.
		update_fusion(ad, &input.sample, now_ns, delta_ns);

		// Now done.
		os_mutex_unlock(&ad->lock);
	}

	return NULL;
}


/*
 *
 * Device functions.
 *
 */

static void
arduino_get_fusion_pose(struct arduino_device *ad, enum xrt_input_name name, struct xrt_space_relation *out_relation)
{
	out_relation->pose.orientation = ad->fusion.rot;

	//! @todo assuming that orientation is actually currently tracked.
	out_relation->relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
}

static void
arduino_device_destroy(struct xrt_device *xdev)
{
	struct arduino_device *ad = arduino_device(xdev);

	// Destroy the thread object.
	os_thread_helper_destroy(&ad->oth);

	// Now that the thread is not running we can destroy the lock.
	os_mutex_destroy(&ad->lock);

	// Remove the variable tracking.
	u_var_remove_root(ad);

	// Destroy the fusion.
	m_imu_3dof_close(&ad->fusion);

	// Does null checking and zeros.
	os_ble_destroy(&ad->ble);

	free(ad);
}

static void
arduino_device_update_inputs(struct xrt_device *xdev)
{
	struct arduino_device *ad = arduino_device(xdev);

	uint64_t now = os_monotonic_get_ns();

	// Lock the data.
	os_mutex_lock(&ad->lock);

	ad->base.inputs[0].timestamp = now;
	ad->base.inputs[1].timestamp = now;
	ad->base.inputs[2].timestamp = now;
	ad->base.inputs[3].timestamp = now;
	ad->base.inputs[4].timestamp = now;
	ad->base.inputs[5].timestamp = now;
	ad->base.inputs[6].timestamp = now;
	ad->base.inputs[7].timestamp = now;

	// Done now.
	os_mutex_unlock(&ad->lock);
}

static void
arduino_device_get_tracked_pose(struct xrt_device *xdev,
                                enum xrt_input_name name,
                                uint64_t at_timestamp_ns,
                                struct xrt_space_relation *out_relation)
{
	struct arduino_device *ad = arduino_device(xdev);

	(void)at_timestamp_ns;
	arduino_get_fusion_pose(ad, name, out_relation);
}


/*
 *
 * Bindings
 *
 */

static struct xrt_binding_input_pair simple_inputs[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_DAYDREAM_BAR_CLICK},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_DAYDREAM_CIRCLE_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_DAYDREAM_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_DAYDREAM_POSE},
};

static struct xrt_binding_profile binding_profiles[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs,
        .num_inputs = ARRAY_SIZE(simple_inputs),
        .outputs = NULL,
        .num_outputs = 0,
    },
};


/*
 *
 * Prober functions.
 *
 */

struct xrt_device *
arduino_device_create(struct os_ble_device *ble)
{
	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)(U_DEVICE_ALLOC_TRACKING_NONE);
	struct arduino_device *ad = U_DEVICE_ALLOCATE(struct arduino_device, flags, 8, 0);

	ad->base.name = XRT_DEVICE_DAYDREAM;
	ad->base.destroy = arduino_device_destroy;
	ad->base.update_inputs = arduino_device_update_inputs;
	ad->base.get_tracked_pose = arduino_device_get_tracked_pose;
	ad->base.inputs[0].name = XRT_INPUT_DAYDREAM_POSE;
	ad->base.inputs[1].name = XRT_INPUT_DAYDREAM_TOUCHPAD_CLICK;
	ad->base.inputs[2].name = XRT_INPUT_DAYDREAM_BAR_CLICK;
	ad->base.inputs[3].name = XRT_INPUT_DAYDREAM_CIRCLE_CLICK;
	ad->base.inputs[4].name = XRT_INPUT_DAYDREAM_VOLDN_CLICK;
	ad->base.inputs[5].name = XRT_INPUT_DAYDREAM_VOLUP_CLICK;
	ad->base.inputs[6].name = XRT_INPUT_DAYDREAM_TOUCHPAD;
	ad->base.binding_profiles = binding_profiles;
	ad->base.num_binding_profiles = ARRAY_SIZE(binding_profiles);

	ad->ble = ble;
	ad->ll = debug_get_log_option_arduino_log();

	m_imu_3dof_init(&ad->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_300MS);

#define DEG_TO_RAD ((double)M_PI / 180.0)
	float accel_ticks_to_float = (4.0 * MATH_GRAVITY_M_S2) / INT16_MAX;
	float gyro_ticks_to_float = (2000.0 * DEG_TO_RAD) / INT16_MAX;

	m_imu_pre_filter_init(&ad->pre_filter, accel_ticks_to_float, gyro_ticks_to_float);
	m_imu_pre_filter_set_switch_x_and_y(&ad->pre_filter);

#if 0
	ad->pre_filter.gyro.bias.x = 10 * gyro_ticks_to_float;
	ad->pre_filter.gyro.bias.y = 10 * gyro_ticks_to_float;
#endif

	// Everything done, finally start the thread.
	int ret = os_thread_helper_start(&ad->oth, arduino_run_thread, ad);
	if (ret != 0) {
		ARDUINO_ERROR(ad, "Failed to start thread!");
		arduino_device_destroy(&ad->base);
		return NULL;
	}

	u_var_add_root(ad, "Arduino flexible input device", true);
	u_var_add_gui_header(ad, &ad->gui.last, "Last");
	u_var_add_ro_vec3_f32(ad, &ad->fusion.last.accel, "last.accel");
	u_var_add_ro_vec3_f32(ad, &ad->fusion.last.gyro, "last.gyro");

	ad->base.orientation_tracking_supported = true;
	ad->base.position_tracking_supported = false;
	ad->base.device_type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER;

	ARDUINO_DEBUG(ad, "Created device!");

	return &ad->base;
}
