// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  drv_vive prober code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#include <stdio.h>


#include "util/u_debug.h"

#include "vive_device.h"
#include "vive_controller.h"
#include "vive_prober.h"

static const char VIVE_PRODUCT_STRING[] = "HTC Vive";
static const char VIVE_PRO_PRODUCT_STRING[] = "VIVE Pro";
static const char VALVE_INDEX_PRODUCT_STRING[] = "Index HMD";
static const char VALVE_INDEX_MANUFACTURER_STRING[] = "Valve";
static const char VIVE_MANUFACTURER_STRING[] = "HTC";

DEBUG_GET_ONCE_LOG_OPTION(vive_log, "VIVE_LOG", U_LOGGING_WARN)

static int
log_vive_string(struct xrt_prober *xp, struct xrt_prober_device *dev, enum xrt_prober_string type)
{
	unsigned char s[256] = {0};
	int len = xrt_prober_get_string_descriptor(xp, dev, type, s, sizeof(s));
	if (len > 0)
		U_LOG_I("%s: %s", xrt_prober_string_to_string(type), s);
	return len;
}

static void
log_vive_device(enum u_logging_level log_level, struct xrt_prober *xp, struct xrt_prober_device *dev)
{
	if (log_level > U_LOGGING_INFO)
		return;

	U_LOG_I("====== vive device ======");
	U_LOG_I("Vendor:   %04x", dev->vendor_id);
	U_LOG_I("Product:  %04x", dev->product_id);
	U_LOG_I("Class:    %d", dev->usb_dev_class);
	U_LOG_I("Bus type: %s", xrt_bus_type_to_string(dev->bus));
	log_vive_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER);
	log_vive_string(xp, dev, XRT_PROBER_STRING_PRODUCT);
	log_vive_string(xp, dev, XRT_PROBER_STRING_SERIAL_NUMBER);
}

static int
init_vive1(struct xrt_prober *xp,
           struct xrt_prober_device *dev,
           struct xrt_prober_device **devices,
           size_t num_devices,
           enum u_logging_level ll,
           struct xrt_device **out_xdev)
{
	log_vive_device(ll, xp, dev);

	if (!xrt_prober_match_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER, VIVE_MANUFACTURER_STRING) ||
	    !xrt_prober_match_string(xp, dev, XRT_PROBER_STRING_PRODUCT, VIVE_PRODUCT_STRING)) {
		return -1;
	}

	struct os_hid_device *sensors_dev = NULL;
	struct os_hid_device *watchman_dev = NULL;

	for (uint32_t i = 0; i < num_devices; i++) {
		struct xrt_prober_device *d = devices[i];

		if (d->vendor_id != VALVE_VID && d->product_id != VIVE_LIGHTHOUSE_FPGA_RX)
			continue;

		log_vive_device(ll, xp, d);

		int result = xrt_prober_open_hid_interface(xp, d, 0, &sensors_dev);
		if (result != 0) {
			U_LOG_E("Could not open Vive sensors device.");
			return -1;
		}

		result = xrt_prober_open_hid_interface(xp, d, 1, &watchman_dev);
		if (result != 0) {
			U_LOG_E("Could not open headset watchman device.");
			return -1;
		}

		break;
	}

	if (sensors_dev == NULL) {
		U_LOG_E("Could not find Vive sensors device.");
		return -1;
	}

	if (watchman_dev == NULL) {
		U_LOG_E("Could not find headset watchman device.");
		return -1;
	}

	struct os_hid_device *mainboard_dev = NULL;

	int result = xrt_prober_open_hid_interface(xp, dev, 0, &mainboard_dev);
	if (result != 0) {
		U_LOG_E("Could not open Vive mainboard device.");
		free(sensors_dev);
		return -1;
	}
	struct vive_device *d = vive_device_create(mainboard_dev, sensors_dev, watchman_dev, VIVE_VARIANT_VIVE);
	if (d == NULL) {
		free(sensors_dev);
		free(mainboard_dev);
		return -1;
	}

	*out_xdev = &d->base;

	return 1;
}

static int
init_vive_pro(struct xrt_prober *xp,
              struct xrt_prober_device *dev,
              struct xrt_prober_device **devices,
              size_t num_devices,
              enum u_logging_level ll,
              struct xrt_device **out_xdev)
{
	log_vive_device(ll, xp, dev);

	if (!xrt_prober_match_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER, VIVE_MANUFACTURER_STRING) ||
	    !xrt_prober_match_string(xp, dev, XRT_PROBER_STRING_PRODUCT, VIVE_PRO_PRODUCT_STRING)) {
		U_LOG_D("Vive Pro manufacturer string did not match.");
		return -1;
	}

	struct os_hid_device *sensors_dev = NULL;
	struct os_hid_device *watchman_dev = NULL;

	for (uint32_t i = 0; i < num_devices; i++) {
		struct xrt_prober_device *d = devices[i];

		if (d->vendor_id != VALVE_VID && d->product_id != VIVE_PRO_LHR_PID)
			continue;

		log_vive_device(ll, xp, d);

		int result = xrt_prober_open_hid_interface(xp, d, 0, &sensors_dev);
		if (result != 0) {
			U_LOG_E("Could not open Vive sensors device.");
			return -1;
		}

		result = xrt_prober_open_hid_interface(xp, d, 1, &watchman_dev);
		if (result != 0) {
			U_LOG_E("Could not open headset watchman device.");
			return -1;
		}

		break;
	}

	if (sensors_dev == NULL) {
		U_LOG_E("Could not find Vive Pro sensors device.");
		return -1;
	}

	if (watchman_dev == NULL) {
		U_LOG_E("Could not find headset watchman device.");
		return -1;
	}

	struct os_hid_device *mainboard_dev = NULL;

	int result = xrt_prober_open_hid_interface(xp, dev, 0, &mainboard_dev);
	if (result != 0) {
		U_LOG_E("Could not open Vive mainboard device.");
		free(sensors_dev);
		return -1;
	}
	struct vive_device *d = vive_device_create(mainboard_dev, sensors_dev, watchman_dev, VIVE_VARIANT_PRO);
	if (d == NULL) {
		free(sensors_dev);
		free(mainboard_dev);
		return -1;
	}

	*out_xdev = &d->base;

	return 1;
}

static int
init_valve_index(struct xrt_prober *xp,
                 struct xrt_prober_device *dev,
                 struct xrt_prober_device **devices,
                 size_t num_devices,
                 enum u_logging_level ll,
                 struct xrt_device **out_xdev)
{
	log_vive_device(ll, xp, dev);

	if (!xrt_prober_match_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER, VALVE_INDEX_MANUFACTURER_STRING) ||
	    !xrt_prober_match_string(xp, dev, XRT_PROBER_STRING_PRODUCT, VALVE_INDEX_PRODUCT_STRING)) {
		U_LOG_D("Valve Index manufacturer string did not match.");
		return -1;
	}

	struct os_hid_device *sensors_dev = NULL;
	struct os_hid_device *watchman_dev = NULL;

	int result = xrt_prober_open_hid_interface(xp, dev, 0, &sensors_dev);
	if (result != 0) {
		U_LOG_E("Could not open Index sensors device.");
		return -1;
	}

	result = xrt_prober_open_hid_interface(xp, dev, 1, &watchman_dev);
	if (result != 0) {
		U_LOG_E("Could not open headset watchman device.");
		return -1;
	}

	if (sensors_dev == NULL) {
		U_LOG_E("Could not find Index sensors device.");
		return -1;
	}

	if (watchman_dev == NULL) {
		U_LOG_E("Could not find headset watchman device.");
		return -1;
	}

	struct vive_device *d = vive_device_create(NULL, sensors_dev, watchman_dev, VIVE_VARIANT_INDEX);
	if (d == NULL) {
		return -1;
	}

	*out_xdev = &d->base;

	return 1;
}

int
vive_found(struct xrt_prober *xp,
           struct xrt_prober_device **devices,
           size_t num_devices,
           size_t index,
           cJSON *attached_data,
           struct xrt_device **out_xdev)
{
	struct xrt_prober_device *dev = devices[index];

	enum u_logging_level ll = debug_get_log_option_vive_log();

	log_vive_device(ll, xp, dev);

	if (!xrt_prober_can_open(xp, dev)) {
		U_LOG_E("Could not open Vive device.");
		return -1;
	}

	switch (dev->product_id) {
	case VIVE_PID: return init_vive1(xp, dev, devices, num_devices, ll, out_xdev);
	case VIVE_PRO_MAINBOARD_PID: return init_vive_pro(xp, dev, devices, num_devices, ll, out_xdev);
	case VIVE_PRO_LHR_PID: return init_valve_index(xp, dev, devices, num_devices, ll, out_xdev);
	default: U_LOG_E("No product ids matched %.4x", dev->product_id); return -1;
	}

	return -1;
}

int
vive_controller_found(struct xrt_prober *xp,
                      struct xrt_prober_device **devices,
                      size_t num_devices,
                      size_t index,
                      cJSON *attached_data,
                      struct xrt_device **out_xdevs)
{
	struct xrt_prober_device *dev = devices[index];
	int ret;

	static int controller_num = 0;

	struct os_hid_device *controller_hid = NULL;
	ret = xp->open_hid_interface(xp, dev, 0, &controller_hid);
	if (ret != 0) {
		U_LOG_E("Could not open Vive controller device.");
		return -1;
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

	*out_xdevs = &d->base;

	controller_num++;

	return 1;
}
