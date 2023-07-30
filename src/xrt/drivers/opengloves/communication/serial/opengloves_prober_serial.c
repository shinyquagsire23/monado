// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGloves serial prober implementation.
 * @author Daniel Willmott <web@dan-w.com>
 * @ingroup drv_opengloves
 */

#include <libudev.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "util/u_debug.h"
#include "xrt/xrt_defines.h"

#include "opengloves_prober_serial.h"
#include "opengloves_serial.h"

#define OPENGLOVES_PROBER_LOG_LEVEL U_LOGGING_TRACE

#define OPENGLOVES_ERROR(...) U_LOG_IFL_E(OPENGLOVES_PROBER_LOG_LEVEL, __VA_ARGS__)
#define OPENGLOVES_INFO(...) U_LOG_IFL_I(OPENGLOVES_PROBER_LOG_LEVEL, __VA_ARGS__)

#define OPENGLOVES_TTY_PATH_SIZE 14

static int
opengloves_udev_get_sysattr_u16_base16(struct udev_device *dev, const char *name, uint16_t *out_value)
{
	const char *str = udev_device_get_sysattr_value(dev, name);
	if (str == NULL) {
		return -1;
	}

	*out_value = (uint16_t)strtol(str, NULL, 16);

	return 0;
}

static int
opengloves_serial_device_found(const char *sysfs_path, struct opengloves_communication_device **ocdev)
{
	// ttyUSBx comes after the last / in sysfs_path
	const char *tty_name = strrchr(sysfs_path, '/') + 1;

	char tty_path[OPENGLOVES_TTY_PATH_SIZE] = {0};
	int cx = snprintf(tty_path, OPENGLOVES_TTY_PATH_SIZE, "/dev/%s", tty_name);

	if (cx < 0) {
		OPENGLOVES_ERROR("Failed to create tty path!");
		return 0;
	}

	OPENGLOVES_INFO("Device discovered! Attempting connection to %s", tty_path);

	int ret = opengloves_serial_open(tty_path, ocdev);
	if (ret < 0) {
		OPENGLOVES_ERROR("Failed to connect to serial device, %s", strerror(-ret));
		return 0;
	}

	OPENGLOVES_INFO("Successfully connected to device");

	return 1;
}

int
opengloves_get_serial_devices(uint16_t vid, uint16_t pid, struct opengloves_communication_device **out_ocd)
{
	struct udev *ud = udev_new();

	struct udev_enumerate *tty_enumerate = udev_enumerate_new(ud);

	udev_enumerate_add_match_subsystem(tty_enumerate, "tty");
	udev_enumerate_scan_devices(tty_enumerate);

	struct udev_list_entry *tty_devices;
	tty_devices = udev_enumerate_get_list_entry(tty_enumerate);

	struct udev_list_entry *tty_dev_list_entry;

	int dev_count = 0;
	udev_list_entry_foreach(tty_dev_list_entry, tty_devices)
	{
		const char *sysfs_path = udev_list_entry_get_name(tty_dev_list_entry);
		struct udev_device *raw_dev = udev_device_new_from_syspath(ud, sysfs_path);

		struct udev_device *parent_dev = raw_dev;
		while (parent_dev != NULL) {
			uint16_t vendor_id = 0;
			uint16_t product_id = 0;
			opengloves_udev_get_sysattr_u16_base16(parent_dev, "idVendor", &vendor_id);
			opengloves_udev_get_sysattr_u16_base16(parent_dev, "idProduct", &product_id);

			// if vendor and product id match what was requested
			if (vendor_id == vid && product_id == pid && *out_ocd == NULL)
				dev_count = dev_count + opengloves_serial_device_found(sysfs_path, out_ocd);

			parent_dev = udev_device_get_parent(parent_dev);
		}

		// Need to unref the raw device since we create that.
		udev_device_unref(raw_dev);
	}

	// Both the enumerate and the udev struct needs to be unreferenced.
	udev_enumerate_unref(tty_enumerate);
	udev_unref(ud);

	return dev_count;
}
