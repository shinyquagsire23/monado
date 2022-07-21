// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGloves prober implementation.
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#include "xrt/xrt_prober.h"
#include "xrt/xrt_defines.h"

#include "util/u_config_json.h"
#include "util/u_debug.h"
#include "util/u_system_helpers.h"


#include "opengloves_interface.h"
#include "opengloves_device.h"

#include "communication/serial/opengloves_prober_serial.h"
#include "communication/bluetooth/opengloves_prober_bt.h"

#include "../multi_wrapper/multi.h"

#define OPENGLOVES_PROBER_LOG_LEVEL U_LOGGING_TRACE

#define OPENGLOVES_ERROR(...) U_LOG_IFL_E(OPENGLOVES_PROBER_LOG_LEVEL, __VA_ARGS__)
#define OPENGLOVES_INFO(...) U_LOG_IFL_I(OPENGLOVES_PROBER_LOG_LEVEL, __VA_ARGS__)

#define JSON_VEC3(a, b, c) u_json_get_vec3_array(u_json_get(a, b), c)
#define JSON_QUAT(a, b, c) u_json_get_quat(u_json_get(a, b), c)


static const cJSON *
opengloves_load_config_file(struct u_config_json config_json)
{
	u_config_json_open_or_create_main_file(&config_json);
	if (!config_json.file_loaded) {
		OPENGLOVES_ERROR("Failed to load config file");
		return NULL;
	}

	const cJSON *out_config_json = u_json_get(config_json.root, "config_opengloves");
	if (out_config_json == NULL) {
		return NULL;
	}

	return out_config_json;
}

int
opengloves_create_devices(struct xrt_device **out_xdevs, const struct xrt_system_devices *sysdevs)
{
	struct xrt_device *dev_left = NULL;
	struct xrt_device *dev_right = NULL;

	// first check for serial devices
	struct opengloves_communication_device *ocd_left = NULL;
	struct opengloves_communication_device *ocd_right = NULL;

	// try to find serial devices
	opengloves_get_serial_devices(LUCIDGLOVES_USB_VID, LUCIDGLOVES_USB_L_PID, &ocd_left);
	opengloves_get_serial_devices(LUCIDGLOVES_USB_VID, LUCIDGLOVES_USB_R_PID, &ocd_right);


	// if comm device is still null try search for bluetooth devices to fill it
	if (ocd_left == NULL)
		opengloves_get_bt_devices(LUCIDGLOVES_BT_L_NAME, &ocd_left);
	if (ocd_right == NULL)
		opengloves_get_bt_devices(LUCIDGLOVES_BT_R_NAME, &ocd_right);

	// now try to create the device if we've found a communication device
	if (ocd_left != NULL)
		dev_left = opengloves_device_create(ocd_left, XRT_HAND_LEFT);
	if (ocd_right != NULL)
		dev_right = opengloves_device_create(ocd_right, XRT_HAND_RIGHT);

	// load config
	struct u_config_json config_json = {0};
	const cJSON *opengloves_config_json = opengloves_load_config_file(config_json);

	// set up tracking overrides
	int cur_dev = 0;
	if (dev_left != NULL && sysdevs->roles.left != NULL) {
		struct xrt_quat rot = XRT_QUAT_IDENTITY;
		struct xrt_vec3 pos = XRT_VEC3_ZERO;
		JSON_QUAT(opengloves_config_json, "offset_rot_right", &rot);
		JSON_VEC3(opengloves_config_json, "offset_pos_right", &pos);

		struct xrt_pose offset_pose = {.orientation = rot, .position = pos};

		struct xrt_device *dev_wrap =
		    multi_create_tracking_override(XRT_TRACKING_OVERRIDE_DIRECT, dev_left, sysdevs->roles.left,
		                                   XRT_INPUT_GENERIC_TRACKER_POSE, &offset_pose);

		out_xdevs[cur_dev++] = dev_wrap;
	}

	if (dev_right != NULL && sysdevs->roles.right != NULL) {
		struct xrt_quat rot = XRT_QUAT_IDENTITY;
		struct xrt_vec3 pos = XRT_VEC3_ZERO;
		JSON_QUAT(opengloves_config_json, "offset_rot_right", &rot);
		JSON_VEC3(opengloves_config_json, "offset_pos_right", &pos);

		struct xrt_pose offset_pose = {.orientation = rot, .position = pos};

		struct xrt_device *dev_wrap =
		    multi_create_tracking_override(XRT_TRACKING_OVERRIDE_DIRECT, dev_left, sysdevs->roles.left,
		                                   XRT_INPUT_GENERIC_TRACKER_POSE, &offset_pose);

		out_xdevs[cur_dev++] = dev_wrap;
	}

	return cur_dev;
}
