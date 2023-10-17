// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  drv_vive prober code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#include <stdio.h>


#include "util/u_debug.h"
#include "util/u_prober.h"
#include "util/u_trace_marker.h"

#include "ql_system.h"
#include "ql_hmd.h"
#include "ql_interface.h"
#include "ql_prober.h"

#include "xrt/xrt_config_drivers.h"

#define XRSP_IF_CLASS    (0xFF)
#define XRSP_IF_SUBCLASS (0x89)
#define XRSP_IF_SUBCLASS_2 (0x8A)
#define XRSP_IF_PROTOCOL (0x01)

DEBUG_GET_ONCE_LOG_OPTION(ql_log, "QUEST_LINK_LOG", U_LOGGING_WARN)

static int
log_ql_string(struct xrt_prober *xp, struct xrt_prober_device *dev, enum xrt_prober_string type)
{
	unsigned char s[256] = {0};

	int len = xrt_prober_get_string_descriptor(xp, dev, type, s, sizeof(s));
	if (len > 0) {
		U_LOG_I("%s: %s", u_prober_string_to_string(type), s);
	}

	return len;
}

static void
log_ql_hmd(enum u_logging_level log_level, struct xrt_prober *xp, struct xrt_prober_device *dev)
{
	if (log_level > U_LOGGING_INFO) {
		return;
	}

	U_LOG_I("====== quest link device ======");
	U_LOG_I("Vendor:   %04x", dev->vendor_id);
	U_LOG_I("Product:  %04x", dev->product_id);
	U_LOG_I("Class:    %d", dev->usb_dev_class);
	U_LOG_I("Bus type: %s", u_prober_bus_type_to_string(dev->bus));
	log_ql_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER);
	log_ql_string(xp, dev, XRT_PROBER_STRING_PRODUCT);
	log_ql_string(xp, dev, XRT_PROBER_STRING_SERIAL_NUMBER);
}

static int
init_ql_usb(struct xrt_prober *xp,
           struct xrt_prober_device *dev,
           struct xrt_prober_device **devices,
           size_t device_count,
           enum u_logging_level log_level,
           struct xrt_device **out_vdev)
{
	log_ql_hmd(log_level, xp, dev);

	*out_vdev = NULL;

	int if_num = xrt_prober_find_interface(xp, dev, XRSP_IF_CLASS, XRSP_IF_SUBCLASS, XRSP_IF_PROTOCOL);

	// Newer XRSP versions (firmwares after Quest 3 release) use this one
	if (if_num < 0) {
		if_num = xrt_prober_find_interface(xp, dev, XRSP_IF_CLASS, XRSP_IF_SUBCLASS_2, XRSP_IF_PROTOCOL);
	}

	if (if_num < 0) {
		U_LOG_E("Could not find XRSP interface on Quest Link device.");
		return 0;
	}

	unsigned char hmd_serial_no[XRT_DEVICE_NAME_LEN];
	int result = xrt_prober_get_string_descriptor(xp, dev, XRT_PROBER_STRING_SERIAL_NUMBER, hmd_serial_no,
	                                          XRT_DEVICE_NAME_LEN);
	if (result < 0) {
		QUEST_LINK_WARN("Could not read Quest Link serial number from USB");
		snprintf((char *)hmd_serial_no, XRT_DEVICE_NAME_LEN, "Unknown");
	}

	struct ql_system *d =
	    ql_system_create(xp, dev, hmd_serial_no, if_num);
	if (d == NULL) {
		return 0;
	}

	out_vdev[0] = &d->hmd->base;
	out_vdev[1] = &d->controllers[0]->base;
	out_vdev[2] = &d->controllers[1]->base;
	out_vdev[3] = &d->hands->base;

	return 4;
}

int
ql_found(struct xrt_prober *xp,
          struct xrt_prober_device **devices,
          size_t device_count,
          size_t index,
          cJSON *attached_data,
          struct xrt_device **out_xdev)
{
	XRT_TRACE_MARKER();

	struct xrt_prober_device *dev = devices[index];

	enum u_logging_level log_level = debug_get_log_option_ql_log();

	log_ql_hmd(log_level, xp, dev);

	if (!xrt_prober_can_open(xp, dev)) {
		U_LOG_E("Could not open Quest Link device.");
		return 0;
	}

	struct ql_hmd *vdev = NULL;
	int count = 0;

	switch (dev->product_id) {
	case QUEST_XRSP_PID:
	case QUEST_MTP_XRSP_PID:
	case QUEST_MTP_XRSP_ADB_PID:
	case QUEST_XRSP_ADB_PID: {
		count = init_ql_usb(xp, dev, devices, device_count, log_level, out_xdev);
		break;
	}
	default: U_LOG_E("No product ids matched %.4x", dev->product_id); return 0;
	}

	return count;
}
