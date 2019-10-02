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
#include "vive_prober.h"

static const char VIVE_PRODUCT_STRING[] = "HTC Vive";
static const char VIVE_MANUFACTURER_STRING[] = "HTC";

DEBUG_GET_ONCE_BOOL_OPTION(vive_debug, "VIVE_PRINT_DEBUG", false)

static int
_print_prober_string(struct xrt_prober *xp,
                     struct xrt_prober_device *dev,
                     enum xrt_prober_string type)
{
	unsigned char s[256] = {0};
	int len = xrt_prober_get_string_descriptor(xp, dev, type, s, sizeof(s));
	if (len > 0)
		printf("%s: %s\n", xrt_prober_string_to_string(type), s);
	return len;
}

static void
_print_device_info(struct xrt_prober *xp, struct xrt_prober_device *dev)
{
	printf("========================\n");
	printf("vive: Probing Device\n");
	printf("vive: Vendor %04x\n", dev->vendor_id);
	printf("vive: Product %04x\n", dev->product_id);
	printf("vive: Class %d\n", dev->usb_dev_class);
	printf("vive: %s\n", xrt_bus_type_to_string(dev->bus));
	_print_prober_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER);
	_print_prober_string(xp, dev, XRT_PROBER_STRING_PRODUCT);
	_print_prober_string(xp, dev, XRT_PROBER_STRING_SERIAL_NUMBER);
	printf("========================\n");
}

static int
init_vive1(struct xrt_prober *xp,
           struct xrt_prober_device *dev,
           struct xrt_prober_device **devices,
           size_t num_devices,
           bool print_debug,
           struct xrt_device **out_xdev)
{
	if (print_debug)
		_print_device_info(xp, dev);

	if (!xrt_prober_match_string(xp, dev, XRT_PROBER_STRING_MANUFACTURER,
	                             VIVE_MANUFACTURER_STRING) ||
	    !xrt_prober_match_string(xp, dev, XRT_PROBER_STRING_PRODUCT,
	                             VIVE_PRODUCT_STRING)) {
		return -1;
	}

	struct os_hid_device *sensors_dev = NULL;
	for (uint32_t i = 0; i < num_devices; i++) {
		struct xrt_prober_device *d = devices[i];

		if (d->vendor_id != VALVE_VID &&
		    d->product_id != VIVE_LIGHTHOUSE_FPGA_RX)
			continue;

		if (print_debug)
			_print_device_info(xp, d);

		int result =
		    xrt_prober_open_hid_interface(xp, d, 0, &sensors_dev);
		if (result != 0) {
			VIVE_ERROR("Could not open Vive sensors device.");
			return -1;
		}
		break;
	}

	if (sensors_dev == NULL) {
		VIVE_ERROR("Could not find Vive sensors device.");
		return -1;
	}

	struct os_hid_device *mainboard_dev = NULL;

	int result = xrt_prober_open_hid_interface(xp, dev, 0, &mainboard_dev);
	if (result != 0) {
		VIVE_ERROR("Could not open Vive mainboard device.");
		free(sensors_dev);
		return -1;
	}
	struct vive_device *d =
	    vive_device_create(mainboard_dev, sensors_dev, VIVE_VARIANT_VIVE);
	if (d == NULL) {
		free(sensors_dev);
		free(mainboard_dev);
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
           struct xrt_device **out_xdev)
{
	struct xrt_prober_device *dev = devices[index];

	bool print_debug = debug_get_bool_option_vive_debug();

	if (print_debug)
		_print_device_info(xp, dev);

	if (!xrt_prober_can_open(xp, dev)) {
		VIVE_ERROR("Could not open Vive device.");
		return -1;
	}

	switch (dev->product_id) {
	case VIVE_PID:
		return init_vive1(xp, dev, devices, num_devices, print_debug,
		                  out_xdev);
	default:
		VIVE_ERROR("No product ids matched %.4x\n", dev->product_id);
		return -1;
	}

	return -1;
}
