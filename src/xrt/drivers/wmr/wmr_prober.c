// Copyright 2020-2021, N Madsen.
// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  WMR prober code.
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Nova King <technobaboo@proton.me>
 * @ingroup drv_wmr
 */

#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_prober.h"

#include "os/os_hid.h"

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_prober.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"

#include "wmr_interface.h"
#include "wmr_hmd.h"
#include "wmr_bt_controller.h"
#include "wmr_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
#include "../multi_wrapper/multi.h"
#include "../ht_ctrl_emu/ht_ctrl_emu_interface.h"
#endif


/*
 *
 * Functions.
 *
 */

static bool
is_left(const char *product_name, size_t size)
{
	return strncmp(product_name, WMR_CONTROLLER_LEFT_PRODUCT_STRING, size) == 0;
}

static bool
is_right(const char *product_name, size_t size)
{
	return strncmp(product_name, WMR_CONTROLLER_RIGHT_PRODUCT_STRING, size) == 0;
}

static void
classify_and_assign_controller(struct xrt_prober *xp,
                               struct xrt_prober_device *xpd,
                               struct wmr_bt_controllers_search_results *ctrls)
{
	char buf[256] = {0};
	int result = xrt_prober_get_string_descriptor(xp, xpd, XRT_PROBER_STRING_PRODUCT, (uint8_t *)buf, sizeof(buf));
	if (result <= 0) {
		U_LOG_E("xrt_prober_get_string_descriptor: %i\n\tFailed to get product string!", result);
		return;
	}

	if (is_left(buf, sizeof(buf))) {
		ctrls->left = xpd;
	} else if (is_right(buf, sizeof(buf))) {
		ctrls->right = xpd;
	}
}

static bool
check_and_get_interface(struct xrt_prober_device *device,
                        enum u_logging_level log_level,
                        enum wmr_headset_type *out_hmd_type)
{
	switch (device->vendor_id) {
	case HP_VID:
		U_LOG_IFL_T(log_level, "HP_VID");

		switch (device->product_id) {
		case REVERB_G1_PID: *out_hmd_type = WMR_HEADSET_REVERB_G1; return true;
		case REVERB_G2_PID: *out_hmd_type = WMR_HEADSET_REVERB_G2; return true;
		case VR1000_PID: *out_hmd_type = WMR_HEADSET_HP_VR1000; return true;
		default: U_LOG_IFL_T(log_level, "No matching PID!"); return false;
		}

	case LENOVO_VID:
		U_LOG_IFL_T(log_level, "LENOVO_VID");

		switch (device->product_id) {
		case EXPLORER_PID: *out_hmd_type = WMR_HEADSET_LENOVO_EXPLORER; return true;
		default: U_LOG_IFL_T(log_level, "No matching PID!"); return false;
		}

	case SAMSUNG_VID:
		U_LOG_IFL_T(log_level, "SAMSUNG_VID");

		switch (device->product_id) {
		case ODYSSEY_PLUS_PID: *out_hmd_type = WMR_HEADSET_SAMSUNG_800ZAA; return true;
		case ODYSSEY_PID:
			U_LOG_IFL_W(log_level, "Original Odyssey may not be well-supported - continuing anyway.");
			*out_hmd_type = WMR_HEADSET_SAMSUNG_XE700X3AI;
			return true;
		default: U_LOG_IFL_T(log_level, "No matching PID!"); return false;
		}

	case QUANTA_VID:
		U_LOG_IFL_T(log_level, "QUANTA_VID");

		switch (device->product_id) {
		case MEDION_ERAZER_X1000_PID: *out_hmd_type = WMR_HEADSET_MEDION_ERAZER_X1000; return true;
		default: U_LOG_IFL_T(log_level, "No matching PID!"); return false;
		}

	default: return false;
	}
}

static bool
find_companion_device(struct xrt_prober *xp,
                      struct xrt_prober_device **devices,
                      size_t device_count,
                      enum u_logging_level log_level,
                      enum wmr_headset_type *out_hmd_type,
                      struct xrt_prober_device **out_device)
{
	struct xrt_prober_device *dev = NULL;

	for (size_t i = 0; i < device_count; i++) {
		bool match = false;

		if (devices[i]->bus != XRT_BUS_TYPE_USB) {
			continue;
		}

		match = check_and_get_interface(devices[i], log_level, out_hmd_type);

		if (!match) {
			continue;
		}

		if (dev != NULL) {
			U_LOG_IFL_W(log_level, "Found multiple control devices, using the last.");
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

	U_LOG_IFL_D(log_level, "Found Hololens Sensors' companion device '%s' '%s' (vid %04X, pid %04X)", p_str, m_str,
	            dev->vendor_id, dev->product_id);


	*out_device = dev;

	return dev != NULL;
}


/*
 *
 * 'Exported' builder functions.
 *
 */

void
wmr_find_bt_controller_pair(struct xrt_prober *xp,
                            struct xrt_prober_device **devices,
                            size_t device_count,
                            enum u_logging_level log_level,
                            struct wmr_bt_controllers_search_results *out_wbtcsr)
{
	// Try to pair controllers of the same type.
	struct wmr_bt_controllers_search_results odyssey_ctrls = {0};
	struct wmr_bt_controllers_search_results wmr_ctrls = {0};
	struct wmr_bt_controllers_search_results reverbg2_ctrls = {0};

	for (size_t i = 0; i < device_count; i++) {
		struct xrt_prober_device *xpd = devices[i];

		// All controllers have the Microsoft vendor ID.
		if (xpd->vendor_id != MICROSOFT_VID) {
			continue;
		}

		// Only handle Bluetooth connected controllers here.
		if (xpd->bus != XRT_BUS_TYPE_BLUETOOTH) {
			continue;
		}

		if (xpd->product_id == WMR_CONTROLLER_PID) {
			classify_and_assign_controller(xp, xpd, &wmr_ctrls);
		} else if (xpd->product_id == ODYSSEY_CONTROLLER_PID) {
			classify_and_assign_controller(xp, xpd, &odyssey_ctrls);
		} else if (xpd->product_id == REVERB_G2_CONTROLLER_PID) {
			classify_and_assign_controller(xp, xpd, &reverbg2_ctrls);
		}
	}

	// We have to prefer one type pair, prefer Odyssey.
	if (odyssey_ctrls.left != NULL && odyssey_ctrls.right != NULL) {
		*out_wbtcsr = odyssey_ctrls;
		return;
	}

	if (reverbg2_ctrls.left != NULL && reverbg2_ctrls.right != NULL) {
		*out_wbtcsr = reverbg2_ctrls;
		return;
	}

	// Other type pair.
	if (wmr_ctrls.left != NULL && wmr_ctrls.right != NULL) {
		*out_wbtcsr = wmr_ctrls;
		return;
	}

	// Grab any of them.
	out_wbtcsr->left = reverbg2_ctrls.left != NULL  ? reverbg2_ctrls.left
	                   : odyssey_ctrls.left != NULL ? odyssey_ctrls.left
	                                                : wmr_ctrls.left;
	out_wbtcsr->right = reverbg2_ctrls.right != NULL  ? reverbg2_ctrls.right
	                    : odyssey_ctrls.right != NULL ? odyssey_ctrls.right
	                                                  : wmr_ctrls.right;
}

void
wmr_find_companion_device(struct xrt_prober *xp,
                          struct xrt_prober_device **xpdevs,
                          size_t xpdev_count,
                          enum u_logging_level log_level,
                          struct xrt_prober_device *xpdev_holo,
                          struct wmr_companion_search_results *out_wcsr)
{
	struct xrt_prober_device *xpdev_companion = NULL;
	enum wmr_headset_type type = WMR_HEADSET_GENERIC;

	if (!find_companion_device(xp, xpdevs, xpdev_count, log_level, &type, &xpdev_companion)) {
		U_LOG_IFL_E(log_level, "Did not find HoloLens Sensors' companion device");
		return;
	}

	out_wcsr->xpdev_companion = xpdev_companion;
	out_wcsr->type = type;
}

void
wmr_find_headset(struct xrt_prober *xp,
                 struct xrt_prober_device **xpdevs,
                 size_t xpdev_count,
                 enum u_logging_level log_level,
                 struct wmr_headset_search_results *out_whsr)
{
	struct wmr_companion_search_results wcsr = {0};
	struct xrt_prober_device *xpdev_holo = NULL;

	for (size_t i = 0; i < xpdev_count; i++) {
		struct xrt_prober_device *xpd = xpdevs[i];

		// Only handle Bluetooth connected controllers here.
		if (xpd->bus != XRT_BUS_TYPE_USB) {
			continue;
		}

		if (xpd->vendor_id != MICROSOFT_VID || xpd->product_id != HOLOLENS_SENSORS_PID) {
			continue;
		}

		xpdev_holo = xpd;
		break;
	}

	// Did we find any?
	if (xpdev_holo == NULL) {
		U_LOG_IFL_D(log_level, "Did not find HoloLens Sensors device, no headset connected?");
		return; // Didn't find any hololense device, not an error.
	}


	// Find the companion device.
	wmr_find_companion_device(xp, xpdevs, xpdev_count, log_level, xpdev_holo, &wcsr);
	if (wcsr.xpdev_companion == NULL) {
		U_LOG_IFL_E(log_level, "Found a HoloLens device, but not it's companion device");
		return;
	}

	// Done now, output.
	out_whsr->xpdev_holo = xpdev_holo;
	out_whsr->xpdev_companion = wcsr.xpdev_companion;
	out_whsr->type = wcsr.type;
}


/*
 *
 * 'Exported' create functions.
 *
 */

xrt_result_t
wmr_create_headset(struct xrt_prober *xp,
                   struct xrt_prober_device *xpdev_holo,
                   struct xrt_prober_device *xpdev_companion,
                   enum wmr_headset_type type,
                   enum u_logging_level log_level,
                   struct xrt_device **out_hmd,
                   struct xrt_device **out_left,
                   struct xrt_device **out_right)
{
	DRV_TRACE_MARKER();

	U_LOG_IFL_D(log_level, "Creating headset.");

	const int interface_holo = 2;
	const int interface_companion = 0;
	int ret;

	struct os_hid_device *hid_holo = NULL;
	ret = xrt_prober_open_hid_interface(xp, xpdev_holo, interface_holo, &hid_holo);
	if (ret != 0) {
		U_LOG_IFL_E(log_level, "Failed to open HoloLens Sensors HID interface");
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	struct os_hid_device *hid_companion = NULL;
	ret = xrt_prober_open_hid_interface(xp, xpdev_companion, interface_companion, &hid_companion);
	if (ret != 0) {
		U_LOG_IFL_E(log_level, "Failed to open HoloLens Sensors' companion HID interface.");
		goto error_holo;
	}

	struct xrt_device *hmd = NULL;
	struct xrt_device *ht = NULL;
	struct xrt_device *two_hands[2] = {NULL, NULL}; // Must initialize, always returned.
	wmr_hmd_create(type, hid_holo, hid_companion, xpdev_holo, log_level, &hmd, &ht);

	if (hmd == NULL) {
		U_LOG_IFL_E(log_level, "Failed to create WMR HMD device.");
		/* No cleanup - the wmr_hmd_create() method cleaned up
		 * the hid devices already */
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
	if (ht != NULL) { // Create hand-tracked controllers
		cemu_devices_create(hmd, ht, two_hands);
	}
#endif

	*out_hmd = hmd;
	*out_left = two_hands[0];
	*out_right = two_hands[1];

	return XRT_SUCCESS;

error_holo:
	os_hid_destroy(hid_holo);

	return XRT_ERROR_DEVICE_CREATION_FAILED;
}

xrt_result_t
wmr_create_bt_controller(struct xrt_prober *xp,
                         struct xrt_prober_device *xpdev,
                         enum u_logging_level log_level,
                         struct xrt_device **out_xdev)
{
	DRV_TRACE_MARKER();

	U_LOG_IFL_D(log_level, "Creating Bluetooth controller.");

	struct os_hid_device *hid_controller = NULL;

	// Only handle Bluetooth connected controllers here.
	if (xpdev->bus != XRT_BUS_TYPE_BLUETOOTH) {
		U_LOG_IFL_E(log_level, "Got a non Bluetooth device!");
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	char product_name[256] = {0};
	int ret = xrt_prober_get_string_descriptor( //
	    xp,                                     //
	    xpdev,                                  //
	    XRT_PROBER_STRING_PRODUCT,              //
	    (uint8_t *)product_name,                //
	    sizeof(product_name));                  //

	enum xrt_device_type controller_type = XRT_DEVICE_TYPE_UNKNOWN;
	const int interface_controller = 0;

	switch (xpdev->product_id) {
	case WMR_CONTROLLER_PID:
	case ODYSSEY_CONTROLLER_PID:
	case REVERB_G2_CONTROLLER_PID:
		if (is_left(product_name, sizeof(product_name))) {
			controller_type = XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;
			break;
		} else if (is_right(product_name, sizeof(product_name))) {
			controller_type = XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER;
			break;
		}
	// else fall through
	default:
		U_LOG_IFL_E(log_level,
		            "Unsupported controller device (Bluetooth): vid: 0x%04X, pid: 0x%04X, Product Name: '%s'",
		            xpdev->vendor_id, xpdev->product_id, product_name);
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	ret = xrt_prober_open_hid_interface(xp, xpdev, interface_controller, &hid_controller);
	if (ret != 0) {
		U_LOG_IFL_E(log_level, "Failed to open WMR Bluetooth controller's HID interface");
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	// Takes ownership of the hid_controller, even on failure
	struct xrt_device *xdev =
	    wmr_bt_controller_create(hid_controller, controller_type, xpdev->vendor_id, xpdev->product_id, log_level);
	if (xdev == NULL) {
		U_LOG_IFL_E(log_level, "Failed to create WMR controller (Bluetooth)");
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	*out_xdev = xdev;

	return XRT_SUCCESS;
}
