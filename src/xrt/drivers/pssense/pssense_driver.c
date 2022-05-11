// Copyright 2023, Collabora, Ltd.
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

#include <stdio.h>

/*!
 * @addtogroup drv_pssense
 * @{
 */

#define PSSENSE_TRACE(p, ...) U_LOG_XDEV_IFL_T(&p->base, p->log_level, __VA_ARGS__)
#define PSSENSE_DEBUG(p, ...) U_LOG_XDEV_IFL_D(&p->base, p->log_level, __VA_ARGS__)
#define PSSENSE_WARN(p, ...) U_LOG_XDEV_IFL_W(&p->base, p->log_level, __VA_ARGS__)
#define PSSENSE_ERROR(p, ...) U_LOG_XDEV_IFL_E(&p->base, p->log_level, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(pssense_log, "PSSENSE_LOG", U_LOGGING_WARN)

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
	PSSENSE_INDEX_THUMBSTICK_TOUCH
};

/*!
 * HID data packet.
 */
struct pssense_data_packet
{
	uint8_t header;
	uint8_t thumbstick_x;
	uint8_t thumbstick_y;
	uint8_t trigger_value;
	uint8_t trigger_proximity;
	uint8_t squeeze_proximity;
	uint8_t reserved;
	uint8_t seq_no;
	uint8_t buttons[3];
};

/*!
 * PlayStation Sense state parsed from a data packet.
 */
struct pssense_input_state
{
	uint64_t timestamp;
	uint8_t seq_no;

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

	struct
	{
		bool button_states;
	} gui;
};

/*!
 * Reads one packet from the device, handles time out, locking and checking if
 * the thread has been told to shut down.
 */
static bool
pssense_read_one_packet(struct pssense_device *pssense, uint8_t *buffer, size_t size)
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
		if (ret != (int)size) {
			PSSENSE_ERROR(pssense, "Unexpected HID packet size %i (expected %zu)", ret, size);
			return false;
		}

		return true;
	}

	return false;
}

static void
pssense_parse_packet(struct pssense_device *pssense,
                     struct pssense_data_packet *data,
                     struct pssense_input_state *input)
{
	input->timestamp = os_monotonic_get_ns();
	input->seq_no = data->seq_no;

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
}

static void *
pssense_run_thread(void *ptr)
{
	U_TRACE_SET_THREAD_NAME("PS Sense");

	struct pssense_device *pssense = (struct pssense_device *)ptr;

	union {
		uint8_t buffer[sizeof(struct pssense_data_packet)];
		struct pssense_data_packet input;
	} data;
	struct pssense_input_state input = {0};

	while (os_hid_read(pssense->hid, data.buffer, sizeof(data), 0) > 0) {
		// Empty queue first
	}

	// Now wait for a package to sync up, it's discarded but that's okay.
	if (!pssense_read_one_packet(pssense, data.buffer, sizeof(data))) {
		return NULL;
	}

	while (pssense_read_one_packet(pssense, data.buffer, sizeof(data))) {
		pssense_parse_packet(pssense, (struct pssense_data_packet *)data.buffer, &input);
		os_mutex_lock(&pssense->lock);
		pssense->state = input;
		os_mutex_unlock(&pssense->lock);
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

	for (uint i = 0; i < (uint)sizeof(enum pssense_input_index); i++) {
		pssense->base.inputs[i].timestamp = (int64_t)pssense->state.timestamp;
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
	if (ret != 0) {
		U_LOG_E("Failed to get product name from Bluetooth device!");
		return -1;
	}

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	struct pssense_device *pssense = U_DEVICE_ALLOCATE(struct pssense_device, flags, 13, 1);

	PSSENSE_DEBUG(pssense, "PlayStation Sense controller found");
	pssense->base.destroy = pssense_device_destroy;
	pssense->base.update_inputs = pssense_device_update_inputs;
	pssense->base.name = XRT_DEVICE_PSSENSE;
	snprintf(pssense->base.str, XRT_DEVICE_NAME_LEN, "%s", product_name);

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

	out_xdevs[0] = &pssense->base;
	return 1;
}

/*!
 * @}
 */
