// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Prober code interfacing to libudev.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup st_prober
 */

#include "util/u_misc.h"
#include "p_prober.h"

#include <stdio.h>
#include <string.h>
#include <libudev.h>
#include <inttypes.h>
#include <linux/hidraw.h>


/*
 *
 * Defines
 *
 */

#define HIDRAW_BUS_USB 3
#define HIDRAW_BUS_BLUETOOTH 5


/*
 *
 * Pre-declare functions.
 *
 */

static void
p_udev_add_interface(struct prober_device* pdev,
                     uint32_t interface,
                     const char* path);

static int
p_udev_get_interface_number(struct udev_device* raw_dev,
                            uint16_t* interface_number);

static int
p_udev_get_and_parse_uevent(struct udev_device* raw_dev,
                            uint32_t* out_bus_type,
                            uint16_t* out_vendor_id,
                            uint16_t* out_product_id,
                            uint64_t* out_bluetooth_serial);

static int
p_udev_get_usb_address(struct udev_device* raw_dev,
                       uint32_t bus_type,
                       uint16_t* usb_bus,
                       uint16_t* usb_addr);


/*
 *
 * "Exported" functions.
 *
 */

int
p_udev_probe(struct prober* p)
{
	struct prober_device* pdev;
	struct udev* udev;
	struct udev_enumerate* enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device* raw_dev = NULL;
	uint16_t vendor_id, product_id, interface;
	uint16_t usb_bus = 0;
	uint16_t usb_addr = 0;
	uint32_t bus_type;
	uint64_t bluetooth_id;
	int ret;

	const char* sysfs_path;
	const char* dev_path;

	udev = udev_new();
	if (!udev) {
		P_ERROR(p, "Can't create udev\n");
		return -1;
	}

	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "hidraw");
	udev_enumerate_scan_devices(enumerate);

	devices = udev_enumerate_get_list_entry(enumerate);

	udev_list_entry_foreach(dev_list_entry, devices)
	{
		// Where in the sysfs is.
		sysfs_path = udev_list_entry_get_name(dev_list_entry);
		// Raw sysfs node.
		raw_dev = udev_device_new_from_syspath(udev, sysfs_path);
		// The thing we will open.
		dev_path = udev_device_get_devnode(raw_dev);

		// Bus type, vendor_id and product_id.
		ret = p_udev_get_and_parse_uevent(
		    raw_dev, &bus_type, &vendor_id, &product_id, &bluetooth_id);
		if (ret != 0) {
			P_ERROR(p, "Failed to get uevent info from device");
			goto next;
		}

		// HID interface.
		ret = p_udev_get_interface_number(raw_dev, &interface);
		if (ret != 0) {
			P_ERROR(p, "Failed to get interface number.");
			goto next;
		}

		// Get USB bus and address to de-dublicate devices.
		ret = p_udev_get_usb_address(raw_dev, bus_type, &usb_bus,
		                             &usb_addr);
		if (ret != 0) {
			P_ERROR(p, "Failed to get USB bus and addr.");
			goto next;
		}

		if (bus_type == HIDRAW_BUS_BLUETOOTH) {
			ret = p_dev_get_bluetooth_dev(
			    p, bluetooth_id, vendor_id, product_id, &pdev);
		} else if (bus_type == HIDRAW_BUS_USB) {
			ret = p_dev_get_usb_dev(p, usb_bus, usb_addr, vendor_id,
			                        product_id, &pdev);
		} else {
			// Right now only support USB & Bluetooth devices.
			P_ERROR(p,
			        "Ignoring none USB or Bluetooth hidraw device "
			        "'%u'.",
			        bus_type);
			goto next;
		}

		P_SPEW(p,
		       "hidraw\n"
		       "\t\tptr:          %p (%i)\n"
		       "\t\tsysfs_path:   '%s'\n"
		       "\t\tdev_path:     '%s'\n"
		       "\t\tbus_type:     %i\n"
		       "\t\tvender_id:    %04x\n"
		       "\t\tproduct_id:   %04x\n"
		       "\t\tinterface:    %i\n"
		       "\t\tusb_bus:      %i\n"
		       "\t\tusb_addr:     %i\n"
		       "\t\tbluetooth_id: %012" PRIx64,
		       (void*)pdev, ret, sysfs_path, dev_path, bus_type,
		       vendor_id, product_id, interface, usb_bus, usb_addr,
		       bluetooth_id);

		if (ret != 0) {
			P_ERROR(p, "p_dev_get_usb_device failed!");
			goto next;
		}

		// Add this interface to the usb device.
		p_udev_add_interface(pdev, interface, dev_path);

	next:
		udev_device_unref(raw_dev);
	}

	enumerate = udev_enumerate_unref(enumerate);
	udev = udev_unref(udev);

	return 0;
}


/*
 *
 * Internal functions.
 *
 */

static void
p_udev_add_interface(struct prober_device* pdev,
                     uint32_t interface,
                     const char* path)
{
	size_t new_size =
	    (pdev->num_hidraws + 1) * sizeof(struct prober_hidraw);
	pdev->hidraws = realloc(pdev->hidraws, new_size);

	struct prober_hidraw* hidraw = &pdev->hidraws[pdev->num_hidraws++];
	U_ZERO(hidraw);

	hidraw->interface = interface;
	hidraw->path = strdup(path);
}

static int
p_udev_get_usb_address(struct udev_device* raw_dev,
                       uint32_t bus_type,
                       uint16_t* usb_bus,
                       uint16_t* usb_addr)
{
	struct udev_device* usb_dev;
	const char* bus_str;
	const char* addr_str;


	if (bus_type != HIDRAW_BUS_USB) {
		return 0;
	}

	// Get the first USB device parent.
	// No we should not unref intf_dev, valgrind agrees.
	usb_dev = udev_device_get_parent_with_subsystem_devtype(raw_dev, "usb",
	                                                        "usb_device");
	if (usb_dev == NULL) {
		return -1;
	}

	bus_str = udev_device_get_sysattr_value(usb_dev, "busnum");
	if (bus_str == NULL) {
		return -1;
	}

	addr_str = udev_device_get_sysattr_value(usb_dev, "devnum");
	if (addr_str == NULL) {
		return -1;
	}

	*usb_bus = (int)strtol(bus_str, NULL, 16);
	*usb_addr = (int)strtol(addr_str, NULL, 16);

	return 0;
}

static int
p_udev_get_interface_number(struct udev_device* raw_dev, uint16_t* interface)
{
	struct udev_device* intf_dev;
	const char* str;

	// Make udev find the handle to the interface node.
	// No we should not unref intf_dev, valgrind agrees.
	intf_dev = udev_device_get_parent_with_subsystem_devtype(
	    raw_dev, "usb", "usb_interface");
	if (intf_dev == NULL) {
		return -1;
	}

	str = udev_device_get_sysattr_value(intf_dev, "bInterfaceNumber");
	if (str == NULL) {
		return -1;
	}

	*interface = (uint16_t)strtol(str, NULL, 16);

	return 0;
}

static int
p_udev_get_and_parse_uevent(struct udev_device* raw_dev,
                            uint32_t* out_bus_type,
                            uint16_t* out_vendor_id,
                            uint16_t* out_product_id,
                            uint64_t* out_bluetooth_serial)
{
	struct udev_device* hid_dev;
	char* serial_utf8 = NULL;
	uint64_t bluetooth_serial = 0;
	uint16_t vendor_id = 0, product_id = 0;
	uint32_t bus_type;
	const char* uevent;
	char* saveptr;
	char* line;
	char* tmp;
	int ret;
	bool ok;

	// Dig through and find the regular hid node.
	hid_dev =
	    udev_device_get_parent_with_subsystem_devtype(raw_dev, "hid", NULL);
	if (hid_dev == NULL) {
		return -1;
	}

	uevent = udev_device_get_sysattr_value(hid_dev, "uevent");
	if (uevent == NULL) {
		return -1;
	}

	tmp = strdup(uevent);
	if (tmp == NULL) {
		return -1;
	}

	line = strtok_r(tmp, "\n", &saveptr);
	while (line != NULL) {
		if (strncmp(line, "HID_ID=", 7) == 0) {
			ret = sscanf(line + 7, "%x:%hx:%hx", &bus_type,
			             &vendor_id, &product_id);
			if (ret == 3) {
				ok = true;
			}
		} else if (strncmp(line, "HID_NAME=", 9) == 0) {
			// printf("\t\tprocuct_name: '%s'\n", line + 9);
		} else if (strncmp(line, "HID_UNIQ=", 9) == 0) {
			serial_utf8 = &line[9];
			// printf("\t\tserial: '%s'\n", line + 9);
		}

		line = strtok_r(NULL, "\n", &saveptr);
	}

	if (ok && bus_type == HIDRAW_BUS_BLUETOOTH && serial_utf8 != NULL) {
		union {
			uint8_t arr[8];
			uint64_t v;
		} extract = {0};

		ret = sscanf(serial_utf8, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		             &extract.arr[5], &extract.arr[4], &extract.arr[3],
		             &extract.arr[2], &extract.arr[1], &extract.arr[0]);
		if (ret == 6) {
			bluetooth_serial = extract.v;
		}
	}

	free(tmp);

	if (ok) {
		*out_bus_type = bus_type;
		*out_vendor_id = vendor_id;
		*out_product_id = product_id;
		*out_bluetooth_serial = bluetooth_serial;
		return 0;
	} else {
		return -1;
	}
}
