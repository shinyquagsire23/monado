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

		int num =
		    libusb_get_port_numbers(device, ports, ARRAY_SIZE(ports));

		ret = p_dev_get_usb_dev(p, bus, addr, vendor, product, &pdev);

		P_SPEW(p,
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
