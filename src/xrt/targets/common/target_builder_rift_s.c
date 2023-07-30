// Copyright 2019-2023, Collabora, Ltd.
// Copyright 2022, Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Oculus Rift S prober code.
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "os/os_hid.h"

#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_prober.h"

#include "util/u_builders.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_logging.h"
#include "util/u_system_helpers.h"
#include "util/u_trace_marker.h"

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
#include "ht_ctrl_emu/ht_ctrl_emu_interface.h"
#endif

#include "rift_s/rift_s_interface.h"
#include "rift_s/rift_s.h"

enum u_logging_level rift_s_log_level;

/* Interfaces for the various reports / HID controls */
#define RIFT_S_INTF_HMD 6
#define RIFT_S_INTF_STATUS 7
#define RIFT_S_INTF_CONTROLLERS 8

/*
 *
 * Defines & structs.
 *
 */

DEBUG_GET_ONCE_LOG_OPTION(rift_s_log, "RIFT_S_LOG", U_LOGGING_WARN)

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
DEBUG_GET_ONCE_BOOL_OPTION(rift_s_hand_tracking_as_controller, "RIFT_S_HAND_TRACKING_AS_CONTROLLERS", false)
#endif

static xrt_result_t
rift_s_estimate_system(struct xrt_builder *xb,
                       cJSON *config,
                       struct xrt_prober *xp,
                       struct xrt_builder_estimate *estimate)
{
	struct xrt_prober_device **xpdevs = NULL;
	size_t xpdev_count = 0;
	xrt_result_t xret = XRT_SUCCESS;

	U_ZERO(estimate);

	xret = xrt_prober_lock_list(xp, &xpdevs, &xpdev_count);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	struct xrt_prober_device *dev =
	    u_builder_find_prober_device(xpdevs, xpdev_count, OCULUS_VR_INC_VID, OCULUS_RIFT_S_PID, XRT_BUS_TYPE_USB);
	if (dev != NULL) {
		estimate->certain.head = true;
		estimate->certain.left = true;
		estimate->certain.right = true;
	}

	xret = xrt_prober_unlock_list(xp, &xpdevs);
	assert(xret == XRT_SUCCESS);

	return XRT_SUCCESS;
}

static xrt_result_t
rift_s_open_system(struct xrt_builder *xb,
                   cJSON *config,
                   struct xrt_prober *xp,
                   struct xrt_system_devices **out_xsysd,
                   struct xrt_space_overseer **out_xso)
{
	struct xrt_prober_device **xpdevs = NULL;
	size_t xpdev_count = 0;
	xrt_result_t xret = XRT_SUCCESS;

	assert(out_xsysd != NULL);
	assert(*out_xsysd == NULL);

	DRV_TRACE_MARKER();

	rift_s_log_level = debug_get_log_option_rift_s_log();
	struct u_system_devices *usysd = u_system_devices_allocate();

	xret = xrt_prober_lock_list(xp, &xpdevs, &xpdev_count);
	if (xret != XRT_SUCCESS) {
		goto unlock_and_fail;
	}

	struct xrt_prober_device *dev_hmd =
	    u_builder_find_prober_device(xpdevs, xpdev_count, OCULUS_VR_INC_VID, OCULUS_RIFT_S_PID, XRT_BUS_TYPE_USB);
	if (dev_hmd == NULL) {
		goto unlock_and_fail;
	}

	struct os_hid_device *hid_hmd = NULL;
	int result = xrt_prober_open_hid_interface(xp, dev_hmd, RIFT_S_INTF_HMD, &hid_hmd);
	if (result != 0) {
		RIFT_S_ERROR("Failed to open Rift S HMD interface");
		goto unlock_and_fail;
	}

	struct os_hid_device *hid_status = NULL;
	result = xrt_prober_open_hid_interface(xp, dev_hmd, RIFT_S_INTF_STATUS, &hid_status);
	if (result != 0) {
		os_hid_destroy(hid_hmd);
		RIFT_S_ERROR("Failed to open Rift S status interface");
		goto unlock_and_fail;
	}

	struct os_hid_device *hid_controllers = NULL;
	result = xrt_prober_open_hid_interface(xp, dev_hmd, RIFT_S_INTF_CONTROLLERS, &hid_controllers);
	if (result != 0) {
		os_hid_destroy(hid_hmd);
		os_hid_destroy(hid_status);
		RIFT_S_ERROR("Failed to open Rift S controllers interface");
		goto unlock_and_fail;
	}

	unsigned char hmd_serial_no[XRT_DEVICE_NAME_LEN];
	result = xrt_prober_get_string_descriptor(xp, dev_hmd, XRT_PROBER_STRING_SERIAL_NUMBER, hmd_serial_no,
	                                          XRT_DEVICE_NAME_LEN);
	if (result < 0) {
		RIFT_S_WARN("Could not read Rift S serial number from USB");
		snprintf((char *)hmd_serial_no, XRT_DEVICE_NAME_LEN, "Unknown");
	}

	xret = xrt_prober_unlock_list(xp, &xpdevs);
	if (xret != XRT_SUCCESS) {
		goto fail;
	}

	struct rift_s_system *sys = rift_s_system_create(xp, hmd_serial_no, hid_hmd, hid_status, hid_controllers);
	if (sys == NULL) {
		RIFT_S_ERROR("Failed to initialise Oculus Rift S driver");
		goto fail;
	}

	struct xrt_device *hmd_xdev = rift_s_system_get_hmd(sys);
	usysd->base.xdevs[usysd->base.xdev_count++] = hmd_xdev;
	usysd->base.roles.head = hmd_xdev;

	struct xrt_device *xdev = rift_s_system_get_controller(sys, 0);
	usysd->base.xdevs[usysd->base.xdev_count++] = xdev;
	usysd->base.roles.left = xdev;

	xdev = rift_s_system_get_controller(sys, 1);
	usysd->base.xdevs[usysd->base.xdev_count++] = xdev;
	usysd->base.roles.right = xdev;

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
	struct xrt_device *ht_xdev = rift_s_system_get_hand_tracking_device(sys);
	if (ht_xdev != NULL) {
		// Create hand-tracked controllers
		RIFT_S_DEBUG("Creating emulated hand tracking controllers");

		struct xrt_device *two_hands[2];
		cemu_devices_create(hmd_xdev, ht_xdev, two_hands);

		usysd->base.roles.hand_tracking.left = two_hands[0];
		usysd->base.roles.hand_tracking.right = two_hands[1];

		usysd->base.xdevs[usysd->base.xdev_count++] = two_hands[0];
		usysd->base.xdevs[usysd->base.xdev_count++] = two_hands[1];

		if (debug_get_bool_option_rift_s_hand_tracking_as_controller()) {
			usysd->base.roles.left = two_hands[0];
			usysd->base.roles.right = two_hands[1];
		}
	}
#endif

	*out_xsysd = &usysd->base;
	u_builder_create_space_overseer(&usysd->base, out_xso);

	return XRT_SUCCESS;

unlock_and_fail:
	xret = xrt_prober_unlock_list(xp, &xpdevs);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	/* Fallthrough */
fail:
	u_system_devices_destroy(&usysd);
	return XRT_ERROR_DEVICE_CREATION_FAILED;
}

/*
 *
 * 'Exported' functions.
 *
 */
static const char *driver_list[] = {
    "rift-s",
};

static void
rift_s_destroy(struct xrt_builder *xb)
{
	free(xb);
}

struct xrt_builder *
rift_s_builder_create(void)
{

	struct xrt_builder *xb = U_TYPED_CALLOC(struct xrt_builder);
	xb->estimate_system = rift_s_estimate_system;
	xb->open_system = rift_s_open_system;
	xb->destroy = rift_s_destroy;
	xb->identifier = "rift_s";
	xb->name = "Oculus Rift S";
	xb->driver_identifiers = driver_list;
	xb->driver_identifier_count = ARRAY_SIZE(driver_list);

	return xb;
}
