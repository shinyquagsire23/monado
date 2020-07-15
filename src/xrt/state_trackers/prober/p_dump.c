// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Prober code to dump information.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup st_prober
 */

#include "xrt/xrt_config_os.h"
#include "xrt/xrt_config_have.h"

#include "util/u_misc.h"
#include "p_prober.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>


static int
print_ports(char *tmp, size_t size, uint8_t *ports, int num)
{
	switch (num) {
	case 1: {
		snprintf(tmp, size, "%i", ports[0]);
		return 1;
	}
	case 2: {
		snprintf(tmp, size, "%i.%i", ports[0], ports[1]);
		return 1;
	}
	case 3: {
		snprintf(tmp, size, "%i.%i.%i", ports[0], ports[1], ports[2]);
		return 1;
	}
	case 4: {
		snprintf(tmp, size, "%i.%i.%i.%i", ports[0], ports[1], ports[2],
		         ports[3]);
		return 1;
	}
	case 5: {
		snprintf(tmp, size, "%i.%i.%i.%i.%i", ports[0], ports[1],
		         ports[2], ports[3], ports[4]);
		return 1;
	}
	case 6: {
		snprintf(tmp, size, "%i.%i.%i.%i.%i.%i", ports[0], ports[1],
		         ports[2], ports[3], ports[4], ports[5]);
		return 1;
	}
	case 7: {
		snprintf(tmp, size, "%i.%i.%i.%i.%i.%i.%i", ports[0], ports[1],
		         ports[2], ports[3], ports[4], ports[5], ports[6]);
		return 1;
	}
	default: return 0;
	}
}


/*
 *
 * "Exported" functions.
 *
 */

void
p_dump_device(struct prober *p, struct prober_device *pdev, int id)
{
	char tmp[1024];

	if (pdev->usb.bus != 0 && pdev->usb.addr == 0 &&
	    pdev->base.vendor_id != 0 && pdev->base.product_id == 0) {
		return;
	}

	printf("\t% 3i: 0x%04x:0x%04x\n", id, pdev->base.vendor_id,
	       pdev->base.product_id);
	printf("\t\tptr:              %p\n", (void *)pdev);
	printf("\t\tusb_dev_class:    %02x\n", pdev->base.usb_dev_class);


	if (pdev->usb.serial != NULL || pdev->usb.product != NULL ||
	    pdev->usb.manufacturer != NULL) {
		printf("\t\tusb.product:      %s\n", pdev->usb.product);
		printf("\t\tusb.manufacturer: %s\n", pdev->usb.manufacturer);
		printf("\t\tusb.serial:       %s\n", pdev->usb.serial);
	}

	if (pdev->usb.bus != 0 || pdev->usb.addr != 0) {
		printf("\t\tusb.bus:          %i\n", pdev->usb.bus);
		printf("\t\tusb.addr:         %i\n", pdev->usb.addr);
	}

	if (pdev->bluetooth.id != 0) {
		printf("\t\tbluetooth.id:     %012" PRIx64 "\n",
		       pdev->bluetooth.id);
	}

	int num = pdev->usb.num_ports;
	if (print_ports(tmp, ARRAY_SIZE(tmp), pdev->usb.ports, num)) {
		printf("\t\tport%s            %s\n", num > 1 ? "s:" : ": ",
		       tmp);
	}

#ifdef XRT_HAVE_LIBUSB
	if (pdev->usb.dev != NULL) {
		printf("\t\tlibusb:           %p\n", (void *)pdev->usb.dev);
	}
#endif

#ifdef XRT_HAVE_LIBUVC
	uvc_device_t *uvc_dev = pdev->uvc.dev;
	if (uvc_dev != NULL) {
		struct uvc_device_descriptor *desc;

		printf("\t\tlibuvc:           %p\n", (void *)uvc_dev);

		uvc_get_device_descriptor(uvc_dev, &desc);

		if (desc->product != NULL) {

			printf("\t\tproduct:          '%s'\n", desc->product);
		}
		if (desc->manufacturer != NULL) {

			printf("\t\tmanufacturer:     '%s'\n",
			       desc->manufacturer);
		}
		if (desc->serialNumber != NULL) {

			printf("\t\tserial:           '%s'\n",
			       desc->serialNumber);
		}

		uvc_free_device_descriptor(desc);
		desc = NULL;
	}
#endif

#ifdef XRT_HAVE_V4L2
	for (size_t j = 0; j < pdev->num_v4ls; j++) {
		struct prober_v4l *v4l = &pdev->v4ls[j];

		printf("\t\tv4l.iface:        %i\n", (int)v4l->usb_iface);
		printf("\t\tv4l.index:        %i\n", (int)v4l->v4l_index);
		printf("\t\tv4l.path:         '%s'\n", v4l->path);
	}
#endif

#ifdef XRT_OS_LINUX
	for (size_t j = 0; j < pdev->num_hidraws; j++) {
		struct prober_hidraw *hidraw = &pdev->hidraws[j];

		printf("\t\thidraw.iface:     %i\n", (int)hidraw->interface);
		printf("\t\thidraw.path:      '%s'\n", hidraw->path);
	}
#endif
}
