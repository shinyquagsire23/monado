// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Prober code interfacing to libuvc.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup st_prober
 */

#include "util/u_debug.h"
#include "util/u_misc.h"
#include "p_prober.h"

#include <stdio.h>
#include <string.h>


int
p_libuvc_init(struct prober *p)
{
	return uvc_init(&p->uvc.ctx, p->usb.ctx);
}

void
p_libuvc_teardown(struct prober *p)
{
	// Free all libuvc resources.
	if (p->uvc.list != NULL) {
		uvc_free_device_list(p->uvc.list, 1);
		p->uvc.list = NULL;
	}

	if (p->uvc.ctx != NULL) {
		uvc_exit(p->uvc.ctx);
		p->uvc.ctx = NULL;
	}
}

int
p_libuvc_probe(struct prober *p)
{
	int ret;

	// Free old list first.
	if (p->uvc.list != NULL) {
		uvc_free_device_list(p->uvc.list, 1);
		p->uvc.list = NULL;
	}

	ret = uvc_get_device_list(p->uvc.ctx, &p->uvc.list);
	if (ret < 0) {
		P_ERROR(p, "\tFailed to enumerate uvc devices\n");
		return -1;
	}
	p->uvc.count = 0;
	// Count the number of UVC devices.
	while (p->uvc.list != NULL && p->uvc.list[p->uvc.count] != NULL) {
		p->uvc.count++;
	}

	for (ssize_t k = 0; k < p->uvc.count; k++) {
		uvc_device_t *device = p->uvc.list[k];
		struct uvc_device_descriptor *desc;
		struct prober_device *pdev = NULL;

		uvc_get_device_descriptor(device, &desc);
		uint8_t bus = uvc_get_bus_number(device);
		uint8_t addr = uvc_get_device_address(device);
		uint16_t vendor = desc->idVendor;
		uint16_t product = desc->idProduct;

		ret = p_dev_get_usb_dev(p, bus, addr, vendor, product, &pdev);

		P_TRACE(p,
		        "libuvc\n"
		        "\t\tptr:        %p (%i)\n"
		        "\t\tvendor_id:  %04x\n"
		        "\t\tproduct_id: %04x\n"
		        "\t\tbus:        %i\n"
		        "\t\taddr:       %i\n"
		        "\t\tserial:     %s\n"
		        "\t\tmanuf:      %s\n"
		        "\t\tproduct:    %s",
		        (void *)pdev, ret, vendor, product, bus, addr, desc->serialNumber, desc->manufacturer,
		        desc->product);

		uvc_free_device_descriptor(desc);

		if (ret != 0) {
			P_ERROR(p, "p_dev_get_usb_device failed!");
			continue;
		}

		// Attach the libuvc device to it.
		pdev->uvc.dev = p->uvc.list[k];
	}

	return 0;
}
