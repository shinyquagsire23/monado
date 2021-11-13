// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  WMR prober code.
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_wmr
 */

#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_logging.h"

#include "wmr_interface.h"
#include "wmr_hmd.h"
#include "wmr_bt_controller.h"
#include "wmr_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

/*
 *
 * Defines & structs.
 *
 */

DEBUG_GET_ONCE_LOG_OPTION(wmr_log, "WMR_LOG", U_LOGGING_INFO)


/*
 *
 * Functions.
 *
 */

static bool
check_and_get_interface_hp(struct xrt_prober_device *device, enum wmr_headset_type *out_hmd_type, int *out_interface)
{
	if (device->product_id != REVERB_G1_PID && device->product_id != REVERB_G2_PID) {
		return false;
	}

	if (device->product_id == REVERB_G1_PID)
		*out_hmd_type = WMR_HEADSET_REVERB_G1;
	else
		*out_hmd_type = WMR_HEADSET_REVERB_G2;
	*out_interface = 0;

	return true;
}

static bool
check_and_get_interface_lenovo(struct xrt_prober_device *device,
                               enum wmr_headset_type *out_hmd_type,
                               int *out_interface)
{
	if (device->product_id != EXPLORER_PID) {
		return false;
	}

	*out_hmd_type = WMR_HEADSET_LENOVO_EXPLORER;
	*out_interface = 0;

	return true;
}

static bool
check_and_get_interface_samsung(struct xrt_prober_device *device,
                                enum wmr_headset_type *out_hmd_type,
                                int *out_interface)
{
	if (device->product_id != ODYSSEY_PID) {
		return false;
	}

	*out_hmd_type = WMR_HEADSET_SAMSUNG_800ZAA;
	*out_interface = 0;

	return true;
}

static bool
find_companion_device(struct xrt_prober *xp,
                      struct xrt_prober_device **devices,
                      size_t device_count,
                      enum u_logging_level ll,
                      enum wmr_headset_type *out_hmd_type,
                      struct xrt_prober_device **out_device,
                      int *out_interface)
{
	struct xrt_prober_device *dev = NULL;
	int interface = 0;

	for (size_t i = 0; i < device_count; i++) {
		bool match = false;

		if (devices[i]->bus != XRT_BUS_TYPE_USB) {
			continue;
		}

		switch (devices[i]->vendor_id) {
		case HP_VID: match = check_and_get_interface_hp(devices[i], out_hmd_type, &interface); break;
		case LENOVO_VID: match = check_and_get_interface_lenovo(devices[i], out_hmd_type, &interface); break;
		case SAMSUNG_VID: match = check_and_get_interface_samsung(devices[i], out_hmd_type, &interface); break;
		default: break;
		}

		if (!match) {
			continue;
		}

		if (dev != NULL) {
			U_LOG_IFL_W(ll, "Found multiple control devices, using the last.");
		}
		dev = devices[i];
	}

	if (dev == NULL) {
		return false;
	}

	unsigned char m_str[256] = {0};
	unsigned char p_str[256] = {0};
	xrt_prober_get_string_descriptor(xp, dev, XRT_PROBER_STRING_MANUFACTURER, m_str, sizeof(m_str));
	xrt_prober_get_string_descriptor(xp, dev, XRT_PROBER_STRING_PRODUCT, p_str, sizeof(p_str));

	U_LOG_IFL_D(ll, "Found Hololens Sensors' companion device '%s' '%s' (vid %04X, pid%04X)", p_str, m_str,
	            dev->vendor_id, dev->product_id);


	*out_device = dev;
	*out_interface = interface;

	return dev != NULL;
}


/*
 *
 * Exported functions.
 *
 */

int
wmr_found(struct xrt_prober *xp,
          struct xrt_prober_device **devices,
          size_t device_count,
          size_t index,
          cJSON *attached_data,
          struct xrt_device **out_xdev)
{
	enum u_logging_level ll = debug_get_log_option_wmr_log();

	struct xrt_prober_device *dev_holo = devices[index];
	struct xrt_prober_device *dev_companion = NULL;
	enum wmr_headset_type hmd_type = WMR_HEADSET_GENERIC;
	int interface_holo = 2;
	int interface_companion = 0;

	unsigned char buf[256] = {0};
	int result = xrt_prober_get_string_descriptor(xp, dev_holo, XRT_PROBER_STRING_PRODUCT, buf, sizeof(buf));

	if (!xrt_prober_match_string(xp, dev_holo, XRT_PROBER_STRING_MANUFACTURER, MS_HOLOLENS_MANUFACTURER_STRING) ||
	    !xrt_prober_match_string(xp, dev_holo, XRT_PROBER_STRING_PRODUCT, MS_HOLOLENS_PRODUCT_STRING)) {
		U_LOG_IFL_E(ll, "HoloLens Sensors manufacturer or product strings did not match");
		return -1;
	}

	U_LOG_IFL_D(ll, "Found HoloLens Sensors HMD device '%s' '%s' (vid %04X, pid %04X)",
	            MS_HOLOLENS_MANUFACTURER_STRING, MS_HOLOLENS_PRODUCT_STRING, dev_holo->vendor_id,
	            dev_holo->product_id);

	if (!find_companion_device(xp, devices, device_count, ll, &hmd_type, &dev_companion, &interface_companion)) {
		U_LOG_IFL_E(ll, "Did not find HoloLens Sensors' companion device");
		return -1;
	}

	struct os_hid_device *hid_holo = NULL;
	result = xrt_prober_open_hid_interface(xp, dev_holo, interface_holo, &hid_holo);
	if (result != 0) {
		U_LOG_IFL_E(ll, "Failed to open HoloLens Sensors HID interface");
		return -1;
	}

	struct os_hid_device *hid_companion = NULL;
	result = xrt_prober_open_hid_interface(xp, dev_companion, interface_companion, &hid_companion);
	if (result != 0) {
		U_LOG_IFL_E(ll, "Failed to open HoloLens Sensors' companion HID interface.");
		return -1;
	}

	struct xrt_device *p = wmr_hmd_create(hmd_type, hid_holo, hid_companion, ll);
	if (!p) {
		U_LOG_IFL_E(ll, "Failed to create WMR HMD device.");
		return -1;
	}

	*out_xdev = p;
	return 1;
}

int
wmr_bt_controller_found(struct xrt_prober *xp,
                        struct xrt_prober_device **devices,
                        size_t num_devices,
                        size_t index,
                        cJSON *attached_data,
                        struct xrt_device **out_xdev)
{

	enum u_logging_level ll = debug_get_log_option_wmr_log();

	struct os_hid_device *hid_controller = NULL;

	// Only handle Bluetooth connected controllers here.
	if (devices[index]->bus != XRT_BUS_TYPE_BLUETOOTH) {
		return 0;
	}

	unsigned char product_name[XRT_DEVICE_PRODUCT_NAME_LEN] = {0};
	int ret = xrt_prober_get_string_descriptor(xp, devices[index], XRT_PROBER_STRING_PRODUCT, product_name,
	                                           sizeof(product_name));

	enum xrt_device_type controller_type = XRT_DEVICE_TYPE_UNKNOWN;
	int interface_controller = -1;

	switch (devices[index]->product_id) {
	case WMR_CONTROLLER_PID:
		if (strncmp((char *)product_name, WMR_CONTROLLER_LEFT_PRODUCT_STRING, sizeof(product_name)) == 0) {
			controller_type = XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;
			interface_controller = 0;
			break;
		} else if (strncmp((char *)product_name, WMR_CONTROLLER_RIGHT_PRODUCT_STRING, sizeof(product_name)) ==
		           0) {
			controller_type = XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER;
			interface_controller = 0;
			break;
		}
	// else fall through
	default:
		U_LOG_IFL_D(ll,
		            "Unsupported controller device (Bluetooth): vid: 0x%04X, pid: 0x%04X, Product Name: '%s'",
		            devices[index]->vendor_id, devices[index]->product_id, product_name);
		return -1;
	}



	ret = xrt_prober_open_hid_interface(xp, devices[index], interface_controller, &hid_controller);
	if (ret != 0) {
		U_LOG_IFL_E(ll, "Failed to open WMR Bluetooth controller's HID interface");
		return -1;
	}


	struct xrt_device *p = wmr_bt_controller_create(hid_controller, controller_type, ll);
	if (!p) {
		U_LOG_IFL_E(ll, "Failed to create WMR controller (Bluetooth)");
		return -1;
	}

	*out_xdev = p;
	return 1;
}
