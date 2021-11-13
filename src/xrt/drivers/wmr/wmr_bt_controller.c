// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Driver for Bluetooth based WMR Controller.
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */

#include "xrt/xrt_config_os.h"
#include "xrt/xrt_device.h"

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

#include "wmr_bt_controller.h"
#include "wmr_common.h"
#include "wmr_protocol.h"

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
control_read_packets(struct wmr_bt_controller *d)
{
	unsigned char buffer[WMR_FEATURE_BUFFER_SIZE];

	// Do not block
	int size = os_hid_read(d->controller_hid, buffer, sizeof(buffer), 0);

	if (size < 0) {
		WMR_ERROR(d, "Error reading from controller device");
		return false;
	} else if (size == 0) {
		WMR_TRACE(d, "No more data to read from controller device");
		return true; // No more messages, return.
	} else {
		WMR_DEBUG(d, "Read %u bytes from controller device", size);
	}

	switch (buffer[0]) {
	case WMR_MS_HOLOLENS_MSG_SENSORS: //
		if (size != 45) {
			WMR_ERROR(d, "WMR Controller unexpected message size: %d", size);
			return false;
		}
		WMR_DEBUG(d,
		          "%02x | "                                              // msg type
		          "%02x %02x %02x %02x %02x %02x %02x %02x | "           // buttons and inputs, battery
		          "%02x %02x %02x %02x %02x %02x %02x %02x %02x | "      // accel
		          "%02x %02x | "                                         // temp
		          "%02x %02x %02x %02x %02x %02x %02x %02x %02x | "      // gyro
		          "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x | " // timestamp and more?
		          "%02x %02x %02x %02x %02x %02x",                       // device run state, status and more?
		          buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7],
		          buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15],
		          buffer[16], buffer[17], buffer[18], buffer[19], buffer[20], buffer[21], buffer[22],
		          buffer[23], buffer[24], buffer[25], buffer[26], buffer[27], buffer[28], buffer[29],
		          buffer[30], buffer[31], buffer[32], buffer[33], buffer[34], buffer[35], buffer[36],
		          buffer[37], buffer[38], buffer[39], buffer[40], buffer[41], buffer[42], buffer[43],
		          buffer[44]);

		const unsigned char *p = (unsigned char *)&buffer[1];

		// HP Reverb G1 button mask:
		// Stick_pressed: 0x01
		// Windows button: 0x02
		// Menu button: 0x04
		// Side button: 0x08
		// Touch-pad pressed: 0x10
		// BT pairing button: 0x20
		// Touch-pad touched: 0x40
		uint8_t buttons = read8(&p);

		// Todo: interpret analog stick data
		uint8_t stick_1 = read8(&p);
		uint8_t stick_2 = read8(&p);
		uint8_t stick_3 = read8(&p);

		uint8_t trigger = read8(&p); // pressure: 0x00 - 0xFF

		// Touchpad coords range: 0x00 - 0x64. Both are 0xFF when untouched.
		uint8_t pad_x = read8(&p);
		uint8_t pad_y = read8(&p);
		uint8_t battery = read8(&p);
		int32_t accel_x = read24(&p);
		int32_t accel_y = read24(&p);
		int32_t accel_z = read24(&p);
		int32_t temp = read16(&p);
		int32_t gyro_x = read24(&p);
		int32_t gyro_y = read24(&p);
		int32_t gyro_z = read24(&p);

		uint64_t timestamp = read32(&p); // Maybe only part of timestamp.
		read16(&p);                      // Unknown. Seems to depend on controller orientation.
		read32(&p);                      // Unknown.

		read16(&p); // Unknown. Device state, etc.
		read16(&p);
		read16(&p);

		WMR_DEBUG(d, "timestamp %lu\ttemp %d\taccel x: %f\ty: %f\tz: %f\t\tgyro x: %f\tgyro y: %f\tgyro z: %f",
		          timestamp, temp, accel_x * 0.001f, accel_y * 0.001f, accel_z * 0.001f, gyro_x * 2e-6,
		          gyro_y * 2e-6, gyro_z * 2e-6);

		break;
	default: //
		WMR_DEBUG(d, "Unknown message type: %02x, size: %i from controller device", buffer[0], size);
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
		if (!control_read_packets(d)) {
			break;
		}
	}

	WMR_DEBUG(d, "Exiting reading thread.");

	return NULL;
}


static void
wmr_bt_controller_destroy(struct xrt_device *xdev)
{
	struct wmr_bt_controller *d = wmr_bt_controller(xdev);

	// Destroy the thread object.
	os_thread_helper_destroy(&d->controller_thread);


	if (d->controller_hid != NULL) {
		/* Do any deinit if we have a deinit function */
		//		if (d->hmd_desc && d->hmd_desc->deinit_func) {
		//			d->hmd_desc->deinit_func(d);
		//		}
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

	// Todo: Read config file from controller

	// Thread and other state.
	ret = os_thread_helper_init(&d->controller_thread);
	if (ret != 0) {
		WMR_ERROR(d, "Failed to init WMR controller threading!");
		wmr_bt_controller_destroy(&d->base);
		d = NULL;
		return NULL;
	}

	// Hand over controller device to reading thread.
	ret = os_thread_helper_start(&d->controller_thread, wmr_bt_controller_run_thread, d);
	if (ret != 0) {
		WMR_ERROR(d, "Failed to start WMR controller thread!");
		wmr_bt_controller_destroy(&d->base);
		d = NULL;
		return NULL;
	}


	return &d->base;
}
