// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Prober code interfacing to libusb.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup st_prober
 */

#include "util/u_debug.h"
#include "util/u_misc.h"
#include "p_prober.h"

#include <stdio.h>
#include <string.h>


int
p_libusb_init(struct prober *p)
{
	return libusb_init(&p->usb.ctx);
}

void
p_libusb_teardown(struct prober *p)
{
	// Free all libusb resources.
	if (p->usb.list != NULL) {
		libusb_free_device_list(p->usb.list, 1);
		p->usb.list = NULL;
	}

	if (p->usb.ctx != NULL) {
		libusb_exit(p->usb.ctx);
		p->usb.ctx = NULL;
	}
}

int
p_libusb_probe(struct prober *p)
{
	int ret;

	// Free old list first.
	if (p->usb.list != NULL) {
		libusb_free_device_list(p->usb.list, 1);
		p->usb.list = NULL;
	}

	// Probe for USB devices.
	p->usb.count = libusb_get_device_list(p->usb.ctx, &p->usb.list);
	if (p->usb.count < 0) {
		P_ERROR(p, "\tFailed to enumerate usb devices\n");
		return -1;
	}

	for (ssize_t i = 0; i < p->usb.count; i++) {
		libusb_device *device = p->usb.list[i];
		struct libusb_device_descriptor desc;
		struct prober_device *pdev = NULL;

		libusb_get_device_descriptor(device, &desc);
		uint8_t bus = libusb_get_bus_number(device);
		uint8_t addr = libusb_get_device_address(device);
		uint16_t vendor = desc.idVendor;
		uint16_t product = desc.idProduct;
		uint8_t ports[8];

		int num = libusb_get_port_numbers(device, ports, ARRAY_SIZE(ports));

		ret = p_dev_get_usb_dev(p, bus, addr, vendor, product, &pdev);

		P_TRACE(p,
		        "libusb\n"
		        "\t\tptr:        %p (%i)\n"
		        "\t\tvendor_id:  %04x\n"
		        "\t\tproduct_id: %04x\n"
		        "\t\tbus:        %i\n"
		        "\t\taddr:       %i",
		        (void *)pdev, ret, vendor, product, bus, addr);

		if (ret != 0) {
			P_ERROR(p, "p_dev_get_usb_device failed!");
			continue;
		}

		pdev->usb.num_ports = num;
		memcpy(pdev->usb.ports, ports, sizeof(uint8_t) * num);

		// Attach the libusb device to it.
		pdev->usb.dev = device;
	}

	return 0;
}

#define ENUM_TO_STR(r)                                                                                                 \
	case r: return #r

static const char *
p_libusb_error_to_string(enum libusb_error e)
{
	switch (e) {
		ENUM_TO_STR(LIBUSB_SUCCESS);
		ENUM_TO_STR(LIBUSB_ERROR_IO);
		ENUM_TO_STR(LIBUSB_ERROR_INVALID_PARAM);
		ENUM_TO_STR(LIBUSB_ERROR_ACCESS);
		ENUM_TO_STR(LIBUSB_ERROR_NO_DEVICE);
		ENUM_TO_STR(LIBUSB_ERROR_NOT_FOUND);
		ENUM_TO_STR(LIBUSB_ERROR_BUSY);
		ENUM_TO_STR(LIBUSB_ERROR_TIMEOUT);
		ENUM_TO_STR(LIBUSB_ERROR_OVERFLOW);
		ENUM_TO_STR(LIBUSB_ERROR_PIPE);
		ENUM_TO_STR(LIBUSB_ERROR_INTERRUPTED);
		ENUM_TO_STR(LIBUSB_ERROR_NO_MEM);
		ENUM_TO_STR(LIBUSB_ERROR_NOT_SUPPORTED);
		ENUM_TO_STR(LIBUSB_ERROR_OTHER);
	}
	return "";
}

int
p_libusb_get_string_descriptor(struct prober *p,
                               struct prober_device *pdev,
                               enum xrt_prober_string which_string,
                               unsigned char *buffer,
                               int length)
{

	struct libusb_device_descriptor desc;

	libusb_device *usb_dev = pdev->usb.dev;
	int result = libusb_get_device_descriptor(usb_dev, &desc);
	if (result < 0) {
		P_ERROR(p, "libusb_get_device_descriptor failed: %s",
		        p_libusb_error_to_string((enum libusb_error)result));
		return result;
	}
	uint8_t which = 0;
	switch (which_string) {
	case XRT_PROBER_STRING_MANUFACTURER: which = desc.iManufacturer; break;
	case XRT_PROBER_STRING_PRODUCT: which = desc.iProduct; break;
	case XRT_PROBER_STRING_SERIAL_NUMBER: which = desc.iSerialNumber; break;
	default: break;
	}
	P_TRACE(p,
	        "libusb\n"
	        "\t\tptr:        %p\n"
	        "\t\trequested string index:  %i",
	        (void *)pdev, which);
	if (which == 0) {
		// Not available?
		return 0;
	}
	libusb_device_handle *dev_handle = NULL;
	result = libusb_open(usb_dev, &dev_handle);
	if (result < 0) {
		P_ERROR(p, "libusb_open failed: %s", p_libusb_error_to_string((enum libusb_error)result));
		return result;
	}
	int string_length = libusb_get_string_descriptor_ascii(dev_handle, which, buffer, length);
	if (string_length < 0) {
		P_ERROR(p, "libusb_get_string_descriptor_ascii failed!");
	}
	libusb_close(dev_handle);
	return string_length;
}

bool
p_libusb_can_open(struct prober *p, struct prober_device *pdev)
{
	libusb_device *usb_dev = pdev->usb.dev;
	int result;
	libusb_device_handle *dev_handle = NULL;
	result = libusb_open(usb_dev, &dev_handle);
	if (result < 0) {
		P_ERROR(p, "libusb_open failed: %s", p_libusb_error_to_string((enum libusb_error)result));
		return false;
	}

	libusb_close(dev_handle);
	return true;
}
