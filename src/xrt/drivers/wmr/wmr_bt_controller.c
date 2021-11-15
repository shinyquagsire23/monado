// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Driver for Bluetooth based WMR Controller.
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */

#include "os/os_time.h"
#include "os/os_hid.h"

#include "math/m_mathinclude.h"
#include "math/m_api.h"
#include "math/m_vec2.h"
#include "math/m_predict.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_device.h"

#include "wmr_common.h"
#include "wmr_bt_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifndef XRT_OS_WINDOWS
#include <unistd.h> // for sleep()
#endif

#define WMR_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->ll, __VA_ARGS__)
#define WMR_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->ll, __VA_ARGS__)
#define WMR_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->ll, __VA_ARGS__)
#define WMR_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->ll, __VA_ARGS__)
#define WMR_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->ll, __VA_ARGS__)


static inline struct wmr_bt_controller *
wmr_bt_controller(struct xrt_device *p)
{
	return (struct wmr_bt_controller *)p;
}

static bool
read_packets(struct wmr_bt_controller *d)
{
	unsigned char buffer[WMR_MOTION_CONTROLLER_MSG_BUFFER_SIZE];

	// Do not block
	int size = os_hid_read(d->controller_hid, buffer, sizeof(buffer), 0);

	if (size < 0) {
		WMR_ERROR(d, "WMR Controller (Bluetooth): Error reading from device");
		return false;
	} else if (size == 0) {
		WMR_TRACE(d, "WMR Controller (Bluetooth): No data to read from device");
		return true; // No more messages, return.
	} else {
		WMR_DEBUG(d, "WMR Controller (Bluetooth): Read %u bytes from device", size);
	}

	switch (buffer[0]) {
	case WMR_BT_MOTION_CONTROLLER_MSG:
		// Note: skipping msg type byte
		if (!wmr_controller_packet_parse(&buffer[1], (size_t)size - 1, &d->controller_message, d->ll)) {
			WMR_ERROR(d, "WMR Controller (Bluetooth): Failed parsing message type: %02x, size: %i",
			          buffer[0], size);
			return false;
		}
		break;
	default:
		WMR_DEBUG(d, "WMR Controller (Bluetooth): Unknown message type: %02x, size: %i", buffer[0], size);
		break;
	}

	return true;
}

static void
wmr_bt_controller_set_output(struct xrt_device *xdev, enum xrt_output_name name, union xrt_output_value *value)
{
	// struct wmr_bt_controller *d = wmr_bt_controller(xdev);
	// Todo: implement
}


static void
wmr_bt_controller_get_tracked_pose(struct xrt_device *xdev,
                                   enum xrt_input_name name,
                                   uint64_t at_timestamp_ns,
                                   struct xrt_space_relation *out_relation)
{
	// struct wmr_bt_controller *d = wmr_bt_controller(xdev);
	// Todo: implement
}

static void
wmr_bt_controller_update_inputs(struct xrt_device *xdev)
{
	// struct wmr_bt_controller *d = wmr_bt_controller(xdev);
	// Todo: implement
}

static void *
wmr_bt_controller_run_thread(void *ptr)
{
	struct wmr_bt_controller *d = wmr_bt_controller(ptr);


	os_thread_helper_lock(&d->controller_thread);
	while (os_thread_helper_is_running_locked(&d->controller_thread)) {
		os_thread_helper_unlock(&d->controller_thread);

		// Does not block.
		if (!read_packets(d)) {
			break;
		}
	}

	WMR_DEBUG(d, "WMR Controller (Bluetooth): Exiting reading thread.");

	return NULL;
}


static void
wmr_bt_controller_destroy(struct xrt_device *xdev)
{
	struct wmr_bt_controller *d = wmr_bt_controller(xdev);

	// Destroy the thread object.
	os_thread_helper_destroy(&d->controller_thread);


	if (d->controller_hid != NULL) {
		os_hid_destroy(d->controller_hid);
		d->controller_hid = NULL;
	}

	// Destroy the fusion.
	m_imu_3dof_close(&d->fusion);

	free(d);
}

struct xrt_device *
wmr_bt_controller_create(struct os_hid_device *controller_hid,
                         enum xrt_device_type controller_type,
                         enum u_logging_level ll)
{

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	struct wmr_bt_controller *d = U_DEVICE_ALLOCATE(struct wmr_bt_controller, flags, 1, 01);

	d->ll = ll;
	d->controller_hid = controller_hid;

	d->base.destroy = wmr_bt_controller_destroy;
	d->base.get_tracked_pose = wmr_bt_controller_get_tracked_pose;
	d->base.set_output = wmr_bt_controller_set_output;
	d->base.update_inputs = wmr_bt_controller_update_inputs;

	d->base.inputs[0].name = XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
	d->base.inputs[1].name = XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;


	d->base.name = XRT_DEVICE_WMR_CONTROLLER;
	d->base.device_type = controller_type;
	d->base.orientation_tracking_supported = true;
	d->base.position_tracking_supported = false;
	d->base.hand_tracking_supported = true;

	m_imu_3dof_init(&d->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);



	int ret = 0;

	// Todo: Read config file from controller if possible.

	// Thread and other state.
	ret = os_thread_helper_init(&d->controller_thread);
	if (ret != 0) {
		WMR_ERROR(d, "WMR Controller (Bluetooth): Failed to init controller threading!");
		wmr_bt_controller_destroy(&d->base);
		d = NULL;
		return NULL;
	}

	// Hand over controller device to reading thread.
	ret = os_thread_helper_start(&d->controller_thread, wmr_bt_controller_run_thread, d);
	if (ret != 0) {
		WMR_ERROR(d, "WMR Controller (Bluetooth): Failed to start controller thread!");
		wmr_bt_controller_destroy(&d->base);
		d = NULL;
		return NULL;
	}


	return &d->base;
}
