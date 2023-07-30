// Copyright 2023, Collabora, Ltd.
// Copyright 2023, Jarett Millard
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  PlayStation Sense controller prober and driver code.
 * @author Jarett Millard <jarett.millard@gmail.com>
 * @ingroup drv_pssense
 */

#include "xrt/xrt_prober.h"

#include "os/os_threading.h"
#include "os/os_hid.h"
#include "os/os_time.h"

#include "math/m_api.h"

#include "tracking/t_imu.h"

#include "util/u_var.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"

#include "pssense_interface.h"
#include "math/m_mathinclude.h"
#include "math/m_space.h"
#include "math/m_imu_3dof.h"

#include <stdio.h>

/*!
 * @addtogroup drv_pssense
 * @{
 */

#define PSSENSE_TRACE(p, ...) U_LOG_XDEV_IFL_T(&p->base, p->log_level, __VA_ARGS__)
#define PSSENSE_DEBUG(p, ...) U_LOG_XDEV_IFL_D(&p->base, p->log_level, __VA_ARGS__)
#define PSSENSE_WARN(p, ...) U_LOG_XDEV_IFL_W(&p->base, p->log_level, __VA_ARGS__)
#define PSSENSE_ERROR(p, ...) U_LOG_XDEV_IFL_E(&p->base, p->log_level, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(pssense_log, "PSSENSE_LOG", U_LOGGING_INFO)

#define DEG_TO_RAD(DEG) (DEG * M_PI / 180.)

static struct xrt_binding_input_pair simple_inputs_pssense[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_PSSENSE_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_PSSENSE_OPTIONS_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_PSSENSE_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_PSSENSE_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs_pssense[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_PSSENSE_VIBRATION},
};

static struct xrt_binding_profile binding_profiles_pssense[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs_pssense,
        .input_count = ARRAY_SIZE(simple_inputs_pssense),
        .outputs = simple_outputs_pssense,
        .output_count = ARRAY_SIZE(simple_outputs_pssense),
    },
};

/*!
 * Indices where each input is in the input list.
 */
enum pssense_input_index
{
	PSSENSE_INDEX_PS_CLICK,
	PSSENSE_INDEX_SHARE_CLICK,
	PSSENSE_INDEX_OPTIONS_CLICK,
	PSSENSE_INDEX_SQUARE_CLICK,
	PSSENSE_INDEX_SQUARE_TOUCH,
	PSSENSE_INDEX_TRIANGLE_CLICK,
	PSSENSE_INDEX_TRIANGLE_TOUCH,
	PSSENSE_INDEX_CROSS_CLICK,
	PSSENSE_INDEX_CROSS_TOUCH,
	PSSENSE_INDEX_CIRCLE_CLICK,
	PSSENSE_INDEX_CIRCLE_TOUCH,
	PSSENSE_INDEX_SQUEEZE_CLICK,
	PSSENSE_INDEX_SQUEEZE_TOUCH,
	PSSENSE_INDEX_SQUEEZE_PROXIMITY,
	PSSENSE_INDEX_TRIGGER_CLICK,
	PSSENSE_INDEX_TRIGGER_TOUCH,
	PSSENSE_INDEX_TRIGGER_VALUE,
	PSSENSE_INDEX_TRIGGER_PROXIMITY,
	PSSENSE_INDEX_THUMBSTICK,
	PSSENSE_INDEX_THUMBSTICK_CLICK,
	PSSENSE_INDEX_THUMBSTICK_TOUCH,
	PSSENSE_INDEX_GRIP_POSE,
	PSSENSE_INDEX_AIM_POSE,
};

const uint8_t INPUT_REPORT_ID = 0x31;
const uint8_t OUTPUT_REPORT_ID = 0x31;
const uint8_t OUTPUT_REPORT_TAG = 0x10;
const uint8_t CALIBRATION_DATA_FEATURE_REPORT_ID = 0x05;
const uint8_t CALIBRATION_DATA_PART_ID_1 = 0;
const uint8_t CALIBRATION_DATA_PART_ID_2 = 0x81;

const uint8_t INPUT_REPORT_CRC32_SEED = 0xa1;
const uint8_t OUTPUT_REPORT_CRC32_SEED = 0xa2;
const uint8_t FEATURE_REPORT_CRC32_SEED = 0xa3;

//! Gyro read value range is +-32768.
const double PSSENSE_GYRO_SCALE_DEG = 180.0 / 1024;
//! Accelerometer read value range is +-32768 and covers +-8 g.
const double PSSENSE_ACCEL_SCALE = MATH_GRAVITY_M_S2 / 4096;

//! Flag bits to enable setting vibration in an output report
const uint8_t VIBRATE_ENABLE_BITS = 0x03;
//! Pure 120Hz vibration
const uint8_t VIBRATE_MODE_HIGH_120HZ = 0x00;
//! Pure 60Hz vibration
const uint8_t VIBRATE_MODE_LOW_60HZ = 0x20;
//! Emulates a legacy vibration motor
const uint8_t VIBRATE_MODE_CLASSIC_RUMBLE = 0x40;
//! Softer rumble emulation, like an engine running
const uint8_t VIBRATE_MODE_DIET_RUMBLE = 0x60;

/**
 * 16-bit little-endian int
 */
struct pssense_i16_le
{
	uint8_t low;
	uint8_t high;
};

/**
 * 32-bit little-endian int
 */
struct pssense_i32_le
{
	uint8_t lowest;
	uint8_t lower;
	uint8_t higher;
	uint8_t highest;
};

#define INPUT_REPORT_LENGTH 78
/*!
 * HID input report data packet.
 */
struct pssense_input_report
{
	uint8_t report_id;
	uint8_t bt_header;
	uint8_t thumbstick_x;
	uint8_t thumbstick_y;
	uint8_t trigger_value;
	uint8_t trigger_proximity;
	uint8_t squeeze_proximity;
	uint8_t unknown1[2]; // Always 0x0001
	uint8_t buttons[3];
	uint8_t unknown2; // Always 0x00
	struct pssense_i32_le seq_no;
	struct pssense_i16_le gyro[3];
	struct pssense_i16_le accel[3];
	uint8_t unknown3[3];
	uint8_t unknown4;      // Increments occasionally
	uint8_t battery_level; // Range appears to be 0x00-0x0e
	uint8_t unknown5[10];
	uint8_t charging_state; // 0x00 when unplugged, 0x20 when charging
	uint8_t unknown6[29];
	struct pssense_i32_le crc;
};
static_assert(sizeof(struct pssense_input_report) == INPUT_REPORT_LENGTH, "Incorrect input report struct length");

#define OUTPUT_REPORT_LENGTH 78
/**
 * HID output report data packet.
 */
struct pssense_output_report
{
	uint8_t report_id;
	uint8_t seq_no;          // High bits only; low bits are always 0
	uint8_t tag;             // Needs to be 0x10. Nobody seems to know why.
	uint8_t vibration_flags; // Vibrate mode and enable flags to set vibrate in this report
	uint8_t unknown;
	uint8_t vibration_amplitude; // Vibration amplitude from 0x00-0xff. Sending 0 turns vibration off.
	uint8_t unknown3[68];
	struct pssense_i32_le crc;
};
static_assert(sizeof(struct pssense_output_report) == OUTPUT_REPORT_LENGTH, "Incorrect output report struct length");

/*!
 * PlayStation Sense state parsed from a data packet.
 */
struct pssense_input_state
{
	uint64_t timestamp_ns;
	uint32_t seq_no;

	bool ps_click;
	bool share_click;
	bool options_click;
	bool square_click;
	bool square_touch;
	bool triangle_click;
	bool triangle_touch;
	bool cross_click;
	bool cross_touch;
	bool circle_click;
	bool circle_touch;
	bool squeeze_click;
	bool squeeze_touch;
	float squeeze_proximity;
	bool trigger_click;
	bool trigger_touch;
	float trigger_value;
	float trigger_proximity;
	bool thumbstick_click;
	bool thumbstick_touch;
	struct xrt_vec2 thumbstick;

	struct xrt_vec3_i32 gyro_raw;
	struct xrt_vec3_i32 accel_raw;
};

/*!
 * A single PlayStation Sense Controller.
 *
 * @implements xrt_device
 */
struct pssense_device
{
	struct xrt_device base;

	struct os_hid_device *hid;
	struct os_thread_helper controller_thread;
	struct os_mutex lock;

	enum
	{
		PSSENSE_HAND_LEFT,
		PSSENSE_HAND_RIGHT
	} hand;

	enum u_logging_level log_level;

	//! Input state parsed from most recent packet
	struct pssense_input_state state;
	//! Last output state sent to device
	struct
	{
		uint8_t next_seq_no;
		uint8_t vibration_amplitude;
		uint8_t vibration_mode;
		uint64_t vibration_end_timestamp_ns;
		uint64_t resend_timestamp_ns;
	} output;

	struct m_imu_3dof fusion;
	struct xrt_pose pose;

	struct
	{
		bool button_states;
		bool tracking;
	} gui;
};

static uint32_t
pssense_i32_le_to_u32(const struct pssense_i32_le *from)
{
	return (uint32_t)(from->lowest | from->lower << 8 | from->higher << 16 | from->highest << 24);
}

static struct pssense_i32_le
pssense_u32_to_i32_le(uint32_t from)
{
	struct pssense_i32_le ret = {
	    .lowest = (from >> 0) & 0x0ff,
	    .lower = (from >> 8) & 0x0ff,
	    .higher = (from >> 16) & 0x0ff,
	    .highest = (from >> 24) & 0x0ff,
	};

	return ret;
}

static int16_t
pssense_i16_le_to_i16(const struct pssense_i16_le *from)
{
	// The cast is important, sign extend properly.
	return (int16_t)(from->low | from->high << 8);
}

const uint32_t CRC_POLYNOMIAL = 0xedb88320;
static uint32_t
crc32_le(uint32_t crc, uint8_t const *p, size_t len)
{
	int i;
	crc ^= 0xffffffff;
	while (len--) {
		crc ^= *p++;
		for (i = 0; i < 8; i++)
			crc = (crc >> 1) ^ ((crc & 1) ? CRC_POLYNOMIAL : 0);
	}
	return crc ^ 0xffffffff;
}

/*!
 * Reads one packet from the device, handles time out, locking and checking if
 * the thread has been told to shut down.
 */
static bool
pssense_read_one_packet(struct pssense_device *pssense, uint8_t *buffer, size_t size, bool check_size)
{
	os_thread_helper_lock(&pssense->controller_thread);

	while (os_thread_helper_is_running_locked(&pssense->controller_thread)) {
		os_thread_helper_unlock(&pssense->controller_thread);

		int ret = os_hid_read(pssense->hid, buffer, size, 1000);

		if (ret == 0) {
			PSSENSE_DEBUG(pssense, "Timeout");

			// Must lock thread before check in a while.
			os_thread_helper_lock(&pssense->controller_thread);
			continue;
		}
		if (ret < 0) {
			PSSENSE_ERROR(pssense, "Failed to read device '%i'!", ret);
			return false;
		}
		// Skip this check if we haven't flushed all the compat mode packets yet, since they're shorter.
		if (check_size && ret != (int)size) {
			PSSENSE_ERROR(pssense, "Unexpected HID packet size %i (expected %zu)", ret, size);
			return false;
		}

		return true;
	}

	return false;
}

static bool
pssense_parse_packet(struct pssense_device *pssense,
                     struct pssense_input_report *data,
                     struct pssense_input_state *input)
{
	if (data->report_id != INPUT_REPORT_ID) {
		PSSENSE_WARN(pssense, "Unrecognized HID report id %u", data->report_id);
		return false;
	}

	uint32_t expected_crc = pssense_i32_le_to_u32(&data->crc);
	uint32_t crc = crc32_le(0, &INPUT_REPORT_CRC32_SEED, 1);
	crc = crc32_le(crc, (uint8_t *)data, sizeof(struct pssense_input_report) - 4);
	if (crc != expected_crc) {
		PSSENSE_WARN(pssense, "CRC mismatch; skipping input. Expected %08X but got %08X", expected_crc, crc);
		return false;
	}

	input->timestamp_ns = os_monotonic_get_ns();

	uint32_t seq_no = pssense_i32_le_to_u32(&data->seq_no);
	if (input->seq_no != 0 && seq_no != input->seq_no + 1) {
		PSSENSE_WARN(pssense, "Missed seq no %u. Previous was %u", seq_no, input->seq_no);
	}
	input->seq_no = seq_no;

	input->ps_click = (data->buttons[1] & 16) != 0;
	input->squeeze_touch = (data->buttons[2] & 8) != 0;
	input->squeeze_proximity = data->squeeze_proximity / 255.0f;
	input->trigger_touch = (data->buttons[1] & 128) != 0;
	input->trigger_value = data->trigger_value / 255.0f;
	input->trigger_proximity = data->trigger_proximity / 255.0f;
	input->thumbstick.x = (data->thumbstick_x - 128) / 128.0f;
	input->thumbstick.y = (data->thumbstick_y - 128) / -128.0f;
	input->thumbstick_touch = (data->buttons[2] & 4) != 0;

	if (pssense->hand == PSSENSE_HAND_LEFT) {
		input->share_click = (data->buttons[1] & 1) != 0;
		input->square_click = (data->buttons[0] & 1) != 0;
		input->square_touch = (data->buttons[2] & 2) != 0;
		input->triangle_click = (data->buttons[0] & 8) != 0;
		input->triangle_touch = (data->buttons[2] & 1) != 0;
		input->squeeze_click = (data->buttons[0] & 16) != 0;
		input->trigger_click = (data->buttons[0] & 64) != 0;
		input->thumbstick_click = (data->buttons[1] & 4) != 0;
	} else if (pssense->hand == PSSENSE_HAND_RIGHT) {
		input->options_click = (data->buttons[1] & 2) != 0;
		input->cross_click = (data->buttons[0] & 2) != 0;
		input->cross_touch = (data->buttons[2] & 2) != 0;
		input->circle_click = (data->buttons[0] & 4) != 0;
		input->circle_touch = (data->buttons[2] & 1) != 0;
		input->squeeze_click = (data->buttons[0] & 32) != 0;
		input->trigger_click = (data->buttons[0] & 128) != 0;
		input->thumbstick_click = (data->buttons[1] & 8) != 0;
	}

	input->gyro_raw.x = pssense_i16_le_to_i16(&data->gyro[0]);
	input->gyro_raw.y = pssense_i16_le_to_i16(&data->gyro[1]);
	input->gyro_raw.z = pssense_i16_le_to_i16(&data->gyro[2]);

	input->accel_raw.x = pssense_i16_le_to_i16(&data->accel[0]);
	input->accel_raw.y = pssense_i16_le_to_i16(&data->accel[1]);
	input->accel_raw.z = pssense_i16_le_to_i16(&data->accel[2]);

	return true;
}

static void
pssense_update_fusion(struct pssense_device *pssense)
{
	struct xrt_vec3 gyro;
	gyro.x = DEG_TO_RAD(pssense->state.gyro_raw.x * PSSENSE_GYRO_SCALE_DEG);
	gyro.y = DEG_TO_RAD(pssense->state.gyro_raw.y * PSSENSE_GYRO_SCALE_DEG);
	gyro.z = DEG_TO_RAD(pssense->state.gyro_raw.z * PSSENSE_GYRO_SCALE_DEG);

	struct xrt_vec3 accel;
	accel.x = pssense->state.accel_raw.x * PSSENSE_ACCEL_SCALE;
	accel.y = pssense->state.accel_raw.y * PSSENSE_ACCEL_SCALE;
	accel.z = pssense->state.accel_raw.z * PSSENSE_ACCEL_SCALE;

	// TODO: Apply correction from calibration data

	m_imu_3dof_update(&pssense->fusion, pssense->state.timestamp_ns, &accel, &gyro);
	pssense->pose.orientation = pssense->fusion.rot;
}

static void
pssense_send_output_report_locked(struct pssense_device *pssense)
{
	uint64_t timestamp_ns = os_monotonic_get_ns();

	if (timestamp_ns >= pssense->output.vibration_end_timestamp_ns) {
		pssense->output.vibration_amplitude = 0;
	}

	struct pssense_output_report report = {0};
	report.report_id = OUTPUT_REPORT_ID;
	report.seq_no = pssense->output.next_seq_no << 4;
	report.tag = OUTPUT_REPORT_TAG;
	report.vibration_flags = pssense->output.vibration_mode | VIBRATE_ENABLE_BITS;
	report.vibration_amplitude = pssense->output.vibration_amplitude;

	pssense->output.next_seq_no = (pssense->output.next_seq_no + 1) % 16;

	uint32_t crc = crc32_le(0, &OUTPUT_REPORT_CRC32_SEED, 1);
	crc = crc32_le(crc, (uint8_t *)&report, sizeof(struct pssense_output_report) - 4);
	report.crc = pssense_u32_to_i32_le(crc);

	PSSENSE_DEBUG(pssense, "Setting vibration amplitude: %u, mode: %02X", pssense->output.vibration_amplitude,
	              pssense->output.vibration_mode);
	int ret = os_hid_write(pssense->hid, (uint8_t *)&report, sizeof(struct pssense_output_report));
	if (ret == sizeof(struct pssense_output_report)) {
		// Controller will vibrate for 5 sec unless we resend the output report. Resend every 2 sec to be safe.
		pssense->output.resend_timestamp_ns = timestamp_ns + 2000000000;
		if (pssense->output.resend_timestamp_ns > pssense->output.vibration_end_timestamp_ns) {
			pssense->output.resend_timestamp_ns = pssense->output.vibration_end_timestamp_ns;
		}
	} else {
		PSSENSE_WARN(pssense, "Failed to send output report: %d", ret);
		pssense->output.resend_timestamp_ns = timestamp_ns;
	}
}

static void *
pssense_run_thread(void *ptr)
{
	U_TRACE_SET_THREAD_NAME("PS Sense");

	struct pssense_device *pssense = (struct pssense_device *)ptr;

	union {
		uint8_t buffer[sizeof(struct pssense_input_report)];
		struct pssense_input_report report;
	} data;
	struct pssense_input_state input_state = {0};

	// The Sense controller starts in compat mode with a different HID report ID and format.
	// We need to discard packets until we get a correct report.
	while (pssense_read_one_packet(pssense, data.buffer, sizeof(data), false) &&
	       data.report.report_id != INPUT_REPORT_ID) {
		PSSENSE_DEBUG(pssense, "Discarding compat mode HID report");
	}

	while (pssense_read_one_packet(pssense, data.buffer, sizeof(data), true)) {
		if (pssense_parse_packet(pssense, &data.report, &input_state)) {
			os_mutex_lock(&pssense->lock);
			pssense->state = input_state;
			pssense_update_fusion(pssense);
			if (pssense->output.vibration_amplitude > 0 &&
			    pssense->state.timestamp_ns >= pssense->output.resend_timestamp_ns) {
				pssense_send_output_report_locked(pssense);
			}
			os_mutex_unlock(&pssense->lock);
		}
	}

	return NULL;
}

static void
pssense_device_destroy(struct xrt_device *xdev)
{
	struct pssense_device *pssense = (struct pssense_device *)xdev;

	// Destroy the thread object.
	os_thread_helper_destroy(&pssense->controller_thread);

	// Now that the thread is not running we can destroy the lock.
	os_mutex_destroy(&pssense->lock);

	m_imu_3dof_close(&pssense->fusion);

	// Remove the variable tracking.
	u_var_remove_root(pssense);

	if (pssense->hid != NULL) {
		os_hid_destroy(pssense->hid);
		pssense->hid = NULL;
	}

	free(pssense);
}

static void
pssense_device_update_inputs(struct xrt_device *xdev)
{
	struct pssense_device *pssense = (struct pssense_device *)xdev;

	// Lock the data.
	os_mutex_lock(&pssense->lock);

	for (uint32_t i = 0; i < (uint32_t)sizeof(enum pssense_input_index); i++) {
		pssense->base.inputs[i].timestamp = (int64_t)pssense->state.timestamp_ns;
	}
	pssense->base.inputs[PSSENSE_INDEX_PS_CLICK].value.boolean = pssense->state.ps_click;
	pssense->base.inputs[PSSENSE_INDEX_SHARE_CLICK].value.boolean = pssense->state.share_click;
	pssense->base.inputs[PSSENSE_INDEX_OPTIONS_CLICK].value.boolean = pssense->state.options_click;
	pssense->base.inputs[PSSENSE_INDEX_SQUARE_CLICK].value.boolean = pssense->state.square_click;
	pssense->base.inputs[PSSENSE_INDEX_SQUARE_TOUCH].value.boolean = pssense->state.square_touch;
	pssense->base.inputs[PSSENSE_INDEX_TRIANGLE_CLICK].value.boolean = pssense->state.triangle_click;
	pssense->base.inputs[PSSENSE_INDEX_TRIANGLE_TOUCH].value.boolean = pssense->state.triangle_touch;
	pssense->base.inputs[PSSENSE_INDEX_CROSS_CLICK].value.boolean = pssense->state.cross_click;
	pssense->base.inputs[PSSENSE_INDEX_CROSS_TOUCH].value.boolean = pssense->state.cross_touch;
	pssense->base.inputs[PSSENSE_INDEX_CIRCLE_CLICK].value.boolean = pssense->state.circle_click;
	pssense->base.inputs[PSSENSE_INDEX_CIRCLE_TOUCH].value.boolean = pssense->state.circle_touch;
	pssense->base.inputs[PSSENSE_INDEX_SQUEEZE_CLICK].value.boolean = pssense->state.squeeze_click;
	pssense->base.inputs[PSSENSE_INDEX_SQUEEZE_TOUCH].value.boolean = pssense->state.squeeze_touch;
	pssense->base.inputs[PSSENSE_INDEX_SQUEEZE_PROXIMITY].value.vec1.x = pssense->state.squeeze_proximity;
	pssense->base.inputs[PSSENSE_INDEX_TRIGGER_CLICK].value.boolean = pssense->state.trigger_click;
	pssense->base.inputs[PSSENSE_INDEX_TRIGGER_TOUCH].value.boolean = pssense->state.trigger_touch;
	pssense->base.inputs[PSSENSE_INDEX_TRIGGER_VALUE].value.vec1.x = pssense->state.trigger_value;
	pssense->base.inputs[PSSENSE_INDEX_TRIGGER_PROXIMITY].value.vec1.x = pssense->state.trigger_proximity;
	pssense->base.inputs[PSSENSE_INDEX_THUMBSTICK].value.vec2 = pssense->state.thumbstick;
	pssense->base.inputs[PSSENSE_INDEX_THUMBSTICK_CLICK].value.boolean = pssense->state.thumbstick_click;
	pssense->base.inputs[PSSENSE_INDEX_THUMBSTICK_TOUCH].value.boolean = pssense->state.thumbstick_touch;

	// Done now.
	os_mutex_unlock(&pssense->lock);
}

static void
pssense_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	struct pssense_device *pssense = (struct pssense_device *)xdev;

	if (name != XRT_OUTPUT_NAME_PSSENSE_VIBRATION) {
		PSSENSE_ERROR(pssense, "Unknown output name requested %u", name);
		return;
	}

	uint8_t vibration_amplitude = (uint8_t)(value->vibration.amplitude * 255.0f);
	uint8_t vibration_mode = VIBRATE_MODE_CLASSIC_RUMBLE;
	if (value->vibration.frequency != XRT_FREQUENCY_UNSPECIFIED) {
		if (value->vibration.frequency <= 70) {
			vibration_mode = VIBRATE_MODE_LOW_60HZ;
		} else if (value->vibration.frequency >= 110) {
			vibration_mode = VIBRATE_MODE_HIGH_120HZ;
		}
	}

	os_mutex_lock(&pssense->lock);
	if (vibration_amplitude != pssense->output.vibration_amplitude ||
	    vibration_mode != pssense->output.vibration_mode) {
		pssense->output.vibration_amplitude = vibration_amplitude;
		pssense->output.vibration_mode = vibration_mode;
		pssense->output.vibration_end_timestamp_ns = os_monotonic_get_ns() + value->vibration.duration_ns;
		pssense_send_output_report_locked(pssense);
	}
	os_mutex_unlock(&pssense->lock);
}

static void
pssense_get_fusion_pose(struct pssense_device *pssense,
                        enum xrt_input_name name,
                        uint64_t at_timestamp_ns,
                        struct xrt_space_relation *out_relation)
{
	out_relation->pose = pssense->pose;
	out_relation->linear_velocity.x = 0.0f;
	out_relation->linear_velocity.y = 0.0f;
	out_relation->linear_velocity.z = 0.0f;

	/*!
	 * @todo This is hack, fusion reports angvel relative to the device but
	 * it needs to be in relation to the base space. Rotating it with the
	 * device orientation is enough to get it into the right space, angular
	 * velocity is a derivative so needs a special rotation.
	 */
	math_quat_rotate_derivative(&pssense->pose.orientation, &pssense->fusion.last.gyro,
	                            &out_relation->angular_velocity);

	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT | XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT);
}

static void
pssense_get_tracked_pose(struct xrt_device *xdev,
                         enum xrt_input_name name,
                         uint64_t at_timestamp_ns,
                         struct xrt_space_relation *out_relation)
{
	struct pssense_device *pssense = (struct pssense_device *)xdev;

	if (name != XRT_INPUT_PSSENSE_AIM_POSE && name != XRT_INPUT_PSSENSE_GRIP_POSE) {
		PSSENSE_ERROR(pssense, "Unknown pose name requested %u", name);
		return;
	}

	struct xrt_relation_chain xrc = {0};
	struct xrt_pose pose_correction = {0};

	// Rotate the grip/aim pose up by 60 degrees around the X axis
	struct xrt_vec3 axis = {1.0, 0, 0};
	math_quat_from_angle_vector(DEG_TO_RAD(60), &axis, &pose_correction.orientation);
	m_relation_chain_push_pose(&xrc, &pose_correction);

	struct xrt_space_relation *rel = m_relation_chain_reserve(&xrc);

	os_mutex_lock(&pssense->lock);
	pssense_get_fusion_pose(pssense, name, at_timestamp_ns, rel);
	os_mutex_unlock(&pssense->lock);

	m_relation_chain_resolve(&xrc, out_relation);
}

/**
 * Retrieving the calibration data report will switch the Sense controller from compat mode into full mode.
 */
bool
pssense_get_calibration_data(struct pssense_device *pssense)
{
	int ret;
	uint8_t buffer[64];
	uint8_t data[(sizeof(buffer) - 2) * 2];
	for (int i = 0; i < 2; i++) {
		ret = os_hid_get_feature(pssense->hid, CALIBRATION_DATA_FEATURE_REPORT_ID, buffer, sizeof(buffer));
		if (ret < 0) {
			PSSENSE_ERROR(pssense, "Failed to retrieve calibration report: %d", ret);
			return false;
		}
		if (ret != sizeof(buffer)) {
			PSSENSE_ERROR(pssense, "Invalid byte count transferred, expected %zu got %d\n", sizeof(buffer),
			              ret);
			return false;
		}
		if (buffer[1] == CALIBRATION_DATA_PART_ID_1) {
			memcpy(data, buffer + 2, sizeof(buffer) - 2);
		} else if (buffer[1] == CALIBRATION_DATA_PART_ID_2) {
			memcpy(data + sizeof(buffer) - 2, buffer + 2, sizeof(buffer) - 2);
		} else {
			PSSENSE_ERROR(pssense, "Unknown calibration data part ID %u", buffer[1]);
			return false;
		}
	}

	// TODO: Parse calibration data into prefiler

	return true;
}

#define SET_INPUT(NAME) (pssense->base.inputs[PSSENSE_INDEX_##NAME].name = XRT_INPUT_PSSENSE_##NAME)

int
pssense_found(struct xrt_prober *xp,
              struct xrt_prober_device **devices,
              size_t device_count,
              size_t index,
              cJSON *attached_data,
              struct xrt_device **out_xdevs)
{
	struct os_hid_device *hid = NULL;
	int ret;

	ret = xrt_prober_open_hid_interface(xp, devices[index], 0, &hid);
	if (ret != 0) {
		return -1;
	}

	unsigned char product_name[128];
	ret = xrt_prober_get_string_descriptor( //
	    xp,                                 //
	    devices[index],                     //
	    XRT_PROBER_STRING_PRODUCT,          //
	    product_name,                       //
	    sizeof(product_name));              //
	if (ret <= 0) {
		U_LOG_E("Failed to get product name from Bluetooth device!");
		return -1;
	}

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	struct pssense_device *pssense = U_DEVICE_ALLOCATE(struct pssense_device, flags, 23, 1);
	PSSENSE_DEBUG(pssense, "PlayStation Sense controller found");

	pssense->base.name = XRT_DEVICE_PSSENSE;
	snprintf(pssense->base.str, XRT_DEVICE_NAME_LEN, "%s", product_name);
	pssense->base.update_inputs = pssense_device_update_inputs;
	pssense->base.set_output = pssense_set_output;
	pssense->base.get_tracked_pose = pssense_get_tracked_pose;
	pssense->base.destroy = pssense_device_destroy;
	pssense->base.orientation_tracking_supported = true;

	pssense->base.binding_profiles = binding_profiles_pssense;
	pssense->base.binding_profile_count = ARRAY_SIZE(binding_profiles_pssense);

	m_imu_3dof_init(&pssense->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	pssense->log_level = debug_get_log_option_pssense_log();
	pssense->hid = hid;

	if (devices[index]->product_id == PSSENSE_PID_LEFT) {
		pssense->base.device_type = XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;
		pssense->hand = PSSENSE_HAND_LEFT;
	} else if (devices[index]->product_id == PSSENSE_PID_RIGHT) {
		pssense->base.device_type = XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER;
		pssense->hand = PSSENSE_HAND_RIGHT;
	} else {
		PSSENSE_ERROR(pssense, "Unable to determine controller type");
		pssense_device_destroy(&pssense->base);
		return -1;
	}

	SET_INPUT(PS_CLICK);
	SET_INPUT(SHARE_CLICK);
	SET_INPUT(OPTIONS_CLICK);
	SET_INPUT(SQUARE_CLICK);
	SET_INPUT(SQUARE_TOUCH);
	SET_INPUT(TRIANGLE_CLICK);
	SET_INPUT(TRIANGLE_TOUCH);
	SET_INPUT(CROSS_CLICK);
	SET_INPUT(CROSS_TOUCH);
	SET_INPUT(CIRCLE_CLICK);
	SET_INPUT(CIRCLE_TOUCH);
	SET_INPUT(SQUEEZE_CLICK);
	SET_INPUT(SQUEEZE_TOUCH);
	SET_INPUT(SQUEEZE_PROXIMITY);
	SET_INPUT(TRIGGER_CLICK);
	SET_INPUT(TRIGGER_TOUCH);
	SET_INPUT(TRIGGER_VALUE);
	SET_INPUT(TRIGGER_PROXIMITY);
	SET_INPUT(THUMBSTICK);
	SET_INPUT(THUMBSTICK_CLICK);
	SET_INPUT(THUMBSTICK_TOUCH);
	SET_INPUT(GRIP_POSE);
	SET_INPUT(AIM_POSE);

	pssense->base.outputs[0].name = XRT_OUTPUT_NAME_PSSENSE_VIBRATION;

	ret = os_mutex_init(&pssense->lock);
	if (ret != 0) {
		PSSENSE_ERROR(pssense, "Failed to init mutex!");
		pssense_device_destroy(&pssense->base);
		return -1;
	}

	ret = os_thread_helper_init(&pssense->controller_thread);
	if (ret != 0) {
		PSSENSE_ERROR(pssense, "Failed to init threading!");
		pssense_device_destroy(&pssense->base);
		return -1;
	}

	ret = os_thread_helper_start(&pssense->controller_thread, pssense_run_thread, pssense);
	if (ret != 0) {
		PSSENSE_ERROR(pssense, "Failed to start thread!");
		pssense_device_destroy(&pssense->base);
		return -1;
	}

	if (!pssense_get_calibration_data(pssense)) {
		PSSENSE_ERROR(pssense, "Failed to retrieve calibration data");
		pssense_device_destroy(&pssense->base);
		return -1;
	}

	u_var_add_root(pssense, pssense->base.str, false);
	u_var_add_log_level(pssense, &pssense->log_level, "Log level");

	u_var_add_gui_header(pssense, &pssense->gui.button_states, "Button States");
	u_var_add_bool(pssense, &pssense->state.ps_click, "PS Click");
	if (pssense->hand == PSSENSE_HAND_LEFT) {
		u_var_add_bool(pssense, &pssense->state.share_click, "Share Click");
		u_var_add_bool(pssense, &pssense->state.square_click, "Square Click");
		u_var_add_bool(pssense, &pssense->state.square_touch, "Square Touch");
		u_var_add_bool(pssense, &pssense->state.triangle_click, "Triangle Click");
		u_var_add_bool(pssense, &pssense->state.triangle_touch, "Triangle Touch");
	} else if (pssense->hand == PSSENSE_HAND_RIGHT) {
		u_var_add_bool(pssense, &pssense->state.options_click, "Options Click");
		u_var_add_bool(pssense, &pssense->state.cross_click, "Cross Click");
		u_var_add_bool(pssense, &pssense->state.cross_touch, "Cross Touch");
		u_var_add_bool(pssense, &pssense->state.circle_click, "Circle Click");
		u_var_add_bool(pssense, &pssense->state.circle_touch, "Circle Touch");
	}
	u_var_add_bool(pssense, &pssense->state.squeeze_click, "Squeeze Click");
	u_var_add_bool(pssense, &pssense->state.squeeze_touch, "Squeeze Touch");
	u_var_add_ro_f32(pssense, &pssense->state.squeeze_proximity, "Squeeze Proximity");
	u_var_add_bool(pssense, &pssense->state.trigger_click, "Trigger Click");
	u_var_add_bool(pssense, &pssense->state.trigger_touch, "Trigger Touch");
	u_var_add_ro_f32(pssense, &pssense->state.trigger_value, "Trigger");
	u_var_add_ro_f32(pssense, &pssense->state.trigger_proximity, "Trigger Proximity");
	u_var_add_ro_f32(pssense, &pssense->state.thumbstick.x, "Thumbstick X");
	u_var_add_ro_f32(pssense, &pssense->state.thumbstick.y, "Thumbstick Y");
	u_var_add_bool(pssense, &pssense->state.thumbstick_click, "Thumbstick Click");
	u_var_add_bool(pssense, &pssense->state.thumbstick_touch, "Thumbstick Touch");

	u_var_add_gui_header(pssense, &pssense->gui.tracking, "Tracking");
	u_var_add_ro_vec3_i32(pssense, &pssense->state.gyro_raw, "Raw Gyro");
	u_var_add_ro_vec3_i32(pssense, &pssense->state.accel_raw, "Raw Accel");
	u_var_add_pose(pssense, &pssense->pose, "Pose");

	out_xdevs[0] = &pssense->base;
	return 1;
}

/*!
 * @}
 */
