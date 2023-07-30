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

#include "vive/vive_common.h"

#include "vive_device.h"
#include "vive_controller.h"
#include "vive_prober.h"

#include "xrt/xrt_config_drivers.h"


static const char VIVE_PRODUCT_STRING[] = "HTC Vive";
static const char VIVE_PRO_PRODUCT_STRING[] = "VIVE Pro";
static const char VIVE_PRO2_PRODUCT_STRING[] = "VIVE Pro 2";
static const char VALVE_INDEX_PRODUCT_STRING[] = "Index HMD";
static const char VALVE_INDEX_MANUFACTURER_STRING[] = "Valve";
static const char VIVE_MANUFACTURER_STRING[] = "HTC";

DEBUG_GET_ONCE_LOG_OPTION(vive_log, "VIVE_LOG", U_LOGGING_WARN)

static int
log_vive_string(struct xrt_prober *xp, struct xrt_prober_device *dev, enum xrt_prober_string type)
{
	unsigned char s[256] = {0};

	int len = xrt_prober_get_string_descriptor(xp, dev, type, s, sizeof(s));
	if (len > 0) {
		U_LOG_I("%s: %s", u_prober_string_to_string(type), s);
	}

	return len;
}

static void
log_vive_device(enum u_logging_level log_level, struct xrt_prober *xp, struct xrt_prober_device *dev)
{
	if (log_level > U_LOGGING_INFO) {
		return;
	}

	U_LOG_I("====== vive device ======");
	U_LOG_I("Vendor:   %04x", dev->vendor_id);
	U_LOG_I("Product:  %04x", dev->product_id);
	U_LOG_I("Class:    %d", dev->usb_dev_class);
	U_LOG_I("Bus type: %s", u_prober_bus_type_to_string(dev->bus));
	log_vive_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER);
	log_vive_string(xp, dev, XRT_PROBER_STRING_PRODUCT);
	log_vive_string(xp, dev, XRT_PROBER_STRING_SERIAL_NUMBER);
}

static void
init_vive1(struct xrt_prober *xp,
           struct xrt_prober_device *dev,
           struct xrt_prober_device **devices,
           size_t device_count,
           enum u_logging_level log_level,
           struct vive_tracking_status tstatus,
           struct vive_source *vs,
           struct vive_device **out_vdev)
{
	log_vive_device(log_level, xp, dev);

	if (!u_prober_match_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER, VIVE_MANUFACTURER_STRING) ||
	    !u_prober_match_string(xp, dev, XRT_PROBER_STRING_PRODUCT, VIVE_PRODUCT_STRING)) {
		return;
	}

	struct os_hid_device *sensors_dev = NULL;
	struct os_hid_device *watchman_dev = NULL;

	for (uint32_t i = 0; i < device_count; i++) {
		struct xrt_prober_device *d = devices[i];

		if (d->vendor_id != VALVE_VID && d->product_id != VIVE_LIGHTHOUSE_FPGA_RX)
			continue;

		log_vive_device(log_level, xp, d);

		int result = xrt_prober_open_hid_interface(xp, d, 0, &sensors_dev);
		if (result != 0) {
			U_LOG_E("Could not open Vive sensors device.");
			return;
		}

		result = xrt_prober_open_hid_interface(xp, d, 1, &watchman_dev);
		if (result != 0) {
			U_LOG_E("Could not open headset watchman device.");
			return;
		}

		break;
	}

	if (sensors_dev == NULL) {
		U_LOG_E("Could not find Vive sensors device.");
		return;
	}

	if (watchman_dev == NULL) {
		U_LOG_E("Could not find headset watchman device.");
		return;
	}

	struct os_hid_device *mainboard_dev = NULL;

	int result = xrt_prober_open_hid_interface(xp, dev, 0, &mainboard_dev);
	if (result != 0) {
		U_LOG_E("Could not open Vive mainboard device.");
		free(sensors_dev);
		return;
	}
	struct vive_device *d =
	    vive_device_create(mainboard_dev, sensors_dev, watchman_dev, VIVE_VARIANT_VIVE, tstatus, vs);
	if (d == NULL) {
		free(sensors_dev);
		free(mainboard_dev);
		return;
	}

	*out_vdev = d;

	return;
}

static void
init_vive_pro(struct xrt_prober *xp,
              struct xrt_prober_device *dev,
              struct xrt_prober_device **devices,
              size_t device_count,
              enum u_logging_level log_level,
              struct vive_tracking_status tstatus,
              struct vive_source *vs,
              struct vive_device **out_vdev)
{
	XRT_TRACE_MARKER();

	log_vive_device(log_level, xp, dev);

	if (!u_prober_match_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER, VIVE_MANUFACTURER_STRING) ||
	    !u_prober_match_string(xp, dev, XRT_PROBER_STRING_PRODUCT, VIVE_PRO_PRODUCT_STRING)) {
		U_LOG_D("Vive Pro manufacturer string did not match.");
		return;
	}

	struct os_hid_device *sensors_dev = NULL;
	struct os_hid_device *watchman_dev = NULL;

	for (uint32_t i = 0; i < device_count; i++) {
		struct xrt_prober_device *d = devices[i];

		if (d->vendor_id != VALVE_VID && d->product_id != VIVE_PRO_LHR_PID)
			continue;

		log_vive_device(log_level, xp, d);

		int result = xrt_prober_open_hid_interface(xp, d, 0, &sensors_dev);
		if (result != 0) {
			U_LOG_E("Could not open Vive sensors device.");
			return;
		}

		result = xrt_prober_open_hid_interface(xp, d, 1, &watchman_dev);
		if (result != 0) {
			U_LOG_E("Could not open headset watchman device.");
			return;
		}

		break;
	}

	if (sensors_dev == NULL) {
		U_LOG_E("Could not find Vive Pro sensors device.");
		return;
	}

	if (watchman_dev == NULL) {
		U_LOG_E("Could not find headset watchman device.");
		return;
	}

	struct os_hid_device *mainboard_dev = NULL;

	int result = xrt_prober_open_hid_interface(xp, dev, 0, &mainboard_dev);
	if (result != 0) {
		U_LOG_E("Could not open Vive mainboard device.");
		free(sensors_dev);
		return;
	}
	struct vive_device *d =
	    vive_device_create(mainboard_dev, sensors_dev, watchman_dev, VIVE_VARIANT_PRO, tstatus, vs);
	if (d == NULL) {
		free(sensors_dev);
		free(mainboard_dev);
		return;
	}

	*out_vdev = d;

	return;
}

static void
init_vive_pro2(struct xrt_prober *xp,
               struct xrt_prober_device *dev,
               struct xrt_prober_device **devices,
               size_t device_count,
               enum u_logging_level log_level,
               struct vive_tracking_status tstatus,
               struct vive_source *vs,
               struct vive_device **out_vdev)
{
	XRT_TRACE_MARKER();

	log_vive_device(log_level, xp, dev);

	if (!u_prober_match_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER, VIVE_MANUFACTURER_STRING) ||
	    !u_prober_match_string(xp, dev, XRT_PROBER_STRING_PRODUCT, VIVE_PRO2_PRODUCT_STRING)) {
		U_LOG_D("Vive Pro 2 manufacturer string did not match.");
		return;
	}

	struct os_hid_device *sensors_dev = NULL;
	struct os_hid_device *watchman_dev = NULL;

	for (uint32_t i = 0; i < device_count; i++) {
		struct xrt_prober_device *d = devices[i];

		if (d->vendor_id != VALVE_VID && d->product_id != VIVE_PRO_LHR_PID)
			continue;

		log_vive_device(log_level, xp, d);

		int result = xrt_prober_open_hid_interface(xp, d, 0, &sensors_dev);
		if (result != 0) {
			U_LOG_E("Could not open Vive Pro 2 sensors device.");
			return;
		}

		result = xrt_prober_open_hid_interface(xp, d, 1, &watchman_dev);
		if (result != 0) {
			U_LOG_E("Could not open headset watchman device.");
			return;
		}

		break;
	}

	if (sensors_dev == NULL) {
		U_LOG_E("Could not find Vive Pro 2 sensors device.");
		return;
	}

	if (watchman_dev == NULL) {
		U_LOG_E("Could not find headset watchman device.");
		return;
	}

	struct os_hid_device *mainboard_dev = NULL;

	int result = xrt_prober_open_hid_interface(xp, dev, 0, &mainboard_dev);
	if (result != 0) {
		U_LOG_E("Could not open Vive mainboard device.");
		free(sensors_dev);
		return;
	}
	struct vive_device *d =
	    vive_device_create(mainboard_dev, sensors_dev, watchman_dev, VIVE_VARIANT_PRO, tstatus, vs);
	if (d == NULL) {
		free(sensors_dev);
		free(mainboard_dev);
		return;
	}

	*out_vdev = d;

	return;
}


static void
init_valve_index(struct xrt_prober *xp,
                 struct xrt_prober_device *dev,
                 struct xrt_prober_device **devices,
                 size_t device_count,
                 enum u_logging_level log_level,
                 struct vive_tracking_status tstatus,
                 struct vive_source *vs,
                 struct vive_device **out_vdev)
{
	XRT_TRACE_MARKER();

	log_vive_device(log_level, xp, dev);

	if (!u_prober_match_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER, VALVE_INDEX_MANUFACTURER_STRING) ||
	    !u_prober_match_string(xp, dev, XRT_PROBER_STRING_PRODUCT, VALVE_INDEX_PRODUCT_STRING)) {
		U_LOG_E("Valve Index manufacturer string did not match.");
		return;
	}

	struct os_hid_device *sensors_dev = NULL;
	struct os_hid_device *watchman_dev = NULL;

	int result = xrt_prober_open_hid_interface(xp, dev, 0, &sensors_dev);
	if (result != 0) {
		U_LOG_E("Could not open Index sensors device.");
		return;
	}

	result = xrt_prober_open_hid_interface(xp, dev, 1, &watchman_dev);
	if (result != 0) {
		U_LOG_E("Could not open headset watchman device.");
		return;
	}

	if (sensors_dev == NULL) {
		U_LOG_E("Could not find Index sensors device.");
		return;
	}

	if (watchman_dev == NULL) {
		U_LOG_E("Could not find headset watchman device.");
		return;
	}

	struct vive_device *d = vive_device_create(NULL, sensors_dev, watchman_dev, VIVE_VARIANT_INDEX, tstatus, vs);
	if (d == NULL) {
		return;
	}

	*out_vdev = d;
}

int
vive_found(struct xrt_prober *xp,
           struct xrt_prober_device **devices,
           size_t device_count,
           size_t index,
           cJSON *attached_data,
           struct vive_tracking_status tstatus,
           struct vive_source *vs,
           struct vive_config **out_vive_config,
           struct xrt_device **out_xdev)
{
	XRT_TRACE_MARKER();

	struct xrt_prober_device *dev = devices[index];

	enum u_logging_level log_level = debug_get_log_option_vive_log();

	log_vive_device(log_level, xp, dev);

	if (!xrt_prober_can_open(xp, dev)) {
		U_LOG_E("Could not open Vive device.");
		return 0;
	}

	struct vive_device *vdev = NULL;

	switch (dev->product_id) {
	case VIVE_PID: {
		init_vive1(xp, dev, devices, device_count, log_level, tstatus, vs, &vdev);
		break;
	}
	case VIVE_PRO_MAINBOARD_PID: {
		init_vive_pro(xp, dev, devices, device_count, log_level, tstatus, vs, &vdev);
		break;
	}
	case VIVE_PRO2_MAINBOARD_PID: {
		init_vive_pro2(xp, dev, devices, device_count, log_level, tstatus, vs, &vdev);
		break;
	}
	case VIVE_PRO_LHR_PID: {
		init_valve_index(xp, dev, devices, device_count, log_level, tstatus, vs, &vdev);
		break;
	}
	default: U_LOG_E("No product ids matched %.4x", dev->product_id); return 0;
	}

	if (vdev == NULL) {
		U_LOG_E("Failed after opening Vive device?");
		return 0;
	}

	*out_vive_config = &vdev->config;
	*out_xdev = &vdev->base;

	return 1;
}

int
vive_controller_found(struct xrt_prober *xp,
                      struct xrt_prober_device **devices,
                      size_t device_count,
                      size_t index,
                      cJSON *attached_data,
                      struct xrt_device **out_xdevs)
{
	XRT_TRACE_MARKER();

	struct xrt_prober_device *dev = devices[index];
	int ret;

	static int controller_num = 0;

	struct os_hid_device *controller_hid = NULL;
	ret = xp->open_hid_interface(xp, dev, 0, &controller_hid);
	if (ret != 0) {
		U_LOG_E("Could not open Vive controller device.");
		return 0;
	}

	enum watchman_gen gen = WATCHMAN_GEN_UNKNOWN;
	if (dev->vendor_id == VALVE_VID && dev->product_id == VIVE_WATCHMAN_DONGLE) {
		gen = WATCHMAN_GEN1;
	} else if (dev->vendor_id == VALVE_VID && dev->product_id == VIVE_WATCHMAN_DONGLE_GEN2) {
		gen = WATCHMAN_GEN2;
	} else {
		U_LOG_E("Unknown watchman gen");
	}

	struct vive_controller_device *d = vive_controller_create(controller_hid, gen, controller_num);

	if (d == NULL) {
		return 0;
	}

	*out_xdevs = &d->base;

	controller_num++;

	return 1;
}
